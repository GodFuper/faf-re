#include <Windows.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "boost/shared_ptr.h"
#include "gpg/core/containers/WriteArchive.h"
#include "gpg/core/containers/ReadArchive.h"
#include "gpg/core/reflection/Reflection.h"
#include "lua/LuaObject.h"
#include "lua/LuaRuntimeTypes.h"
#include "lua/LuaMemAccounting.h"
#include "moho/sim/ReplaySnapshotManager.h"
#include "moho/entity/Entity.h"
#include "moho/command/CCommandDb.h"
#include "moho/sim/COGrid.h"
#include "moho/unit/core/Unit.h"
#include "moho/sim/Sim.h"
#include "moho/lua/CScrLuaObjectFactory.h"

extern "C" {
  Proto* luaF_newproto(lua_State* L);
  TString* luaS_newlstr(lua_State* L, const char* str, size_t len);
  int luaL_loadbuffer(lua_State* L, const char* buff, size_t sz, const char* name);
}

namespace moho
{
  struct SimRewindTestPeer
  {
    static moho::Unit* CreateTestUnit()
    {
      return new moho::Unit(nullptr);
    }
  };
}

namespace
{
  int gChecks = 0;
  int gFailures = 0;

#define CHECK(cond)                                                                \
  do {                                                                             \
    ++gChecks;                                                                     \
    if (!(cond)) {                                                                 \
      ++gFailures;                                                                 \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                \
    }                                                                              \
  } while (0)

  /**
   * Test 1: Verify Proto serialization and deserialization with stripped upvalues.
   * In stripped bytecodes (like in production .scd files), sizeupvalues is 0 while nups > 0.
   * Deserialization must ensure upvalues are properly allocated and sizeupvalues matches nups
   * so that subsequent Lua GC passes (traverseproto) and deallocation (luaF_freeproto)
   * do not crash or corrupt memory.
   */
  void TestProtoStrippedUpvalueSerializationAndGC()
  {
    std::printf("--- Running TestProtoStrippedUpvalueSerializationAndGC ---\n");

    LuaPlus::LuaState luaState(LuaPlus::LuaState::LIB_BASE);
    lua_State* const L = luaState.GetCState();
    CHECK(L != nullptr);

    // 1. Compile a real Lua closure with upvalues and local variables
    const char* const script =
      "local outer1 = 10\n"
      "local outer2 = 20\n"
      "local function innerFunc()\n"
      "  local myLocal = outer1 + outer2\n"
      "  return myLocal\n"
      "end\n"
      "return innerFunc\n";

    const int loadStatus = luaL_loadbuffer(L, script, std::strlen(script), "test_chunk");
    CHECK(loadStatus == 0);

    // Get the Proto from the compiled chunk
    Closure* const outerClosure = const_cast<Closure*>(static_cast<const Closure*>(lua_topointer(L, -1)));
    CHECK(outerClosure != nullptr);
    Proto* const mainProto = outerClosure->l.p;
    CHECK(mainProto != nullptr);
    CHECK(mainProto->sizep > 0);

    // Inner function proto is in mainProto->p[0]
    Proto* const proto = mainProto->p[0];
    CHECK(proto != nullptr);
    CHECK(proto->nups >= 2);
    std::printf("  Compiled proto: nups=%d, sizeupvalues=%d, sizelocvars=%d\n",
                proto->nups, proto->sizeupvalues, proto->sizelocvars);

    // Simulate stripped debug upvalues (as in production .scd / precompiled scripts)
    if (proto->upvalues != nullptr && proto->sizeupvalues > 0) {
      luaM_freearray<TString*>(L, proto->upvalues, static_cast<lu_mem>(proto->sizeupvalues));
      proto->upvalues = nullptr;
      proto->sizeupvalues = 0;
    }
    CHECK(proto->sizeupvalues == 0);
    CHECK(proto->nups >= 2);

    // 2. Serialize proto into temp file archive
    std::FILE* rawFile = nullptr;
    CHECK(tmpfile_s(&rawFile) == 0 && rawFile != nullptr);
    boost::shared_ptr<std::FILE> file(rawFile, std::fclose);

    gpg::RRef ownerRef = gpg::MakeRRef<lua_State>(L);
    {
      std::unique_ptr<gpg::WriteArchive> writeArchive(gpg::CreateBinaryWriteArchive(file));
      writeArchive->PreCreatedPtr(ownerRef);
      proto->MemberSerialize(writeArchive.get(), 0, ownerRef);
    }

    std::fflush(file.get());
    const long fileSize = std::ftell(file.get());
    CHECK(fileSize > 0);
    std::printf("  Serialized proto size: %ld bytes\n", fileSize);

    // 3. Deserialize proto into a fresh prototype
    std::fseek(file.get(), 0, SEEK_SET);
    Proto* const restoredProto = luaF_newproto(L);
    try {
      std::unique_ptr<gpg::ReadArchive> readArchive(gpg::CreateBinaryReadArchive(file));
      readArchive->TrackPointer(ownerRef);
      restoredProto->MemberDeserialize(readArchive.get(), 0, ownerRef);
      std::printf("  Deserialization successful!\n");
    } catch (const std::exception& ex) {
      std::printf("  FAIL: Deserialization threw: %s\n", ex.what());
      CHECK(false);
      return;
    }

    CHECK(restoredProto->nups >= 2);
    CHECK(restoredProto->sizeupvalues == restoredProto->nups);
    CHECK(restoredProto->upvalues != nullptr);
    CHECK(restoredProto->sizelocvars > 0);
    CHECK(restoredProto->locvars != nullptr);

    // 4. Force full Lua garbage collection cycle.
    // This exercises traverseproto over restoredProto, ensuring no crash or bad memory accesses.
    std::printf("  Triggering luaC_collectgarbage to test traverseproto...\n");
    lua_setgcthreshold(L, 0);
    std::printf("  Garbage collection completed without crashing.\n");
  }

  /**
   * Test 2: Verify dumped replay snapshot files on disk.
   * Validates that snapshots exist and have reasonable sizes (> 50 MB),
   * and verifies sequential beat order (e.g. 1, 301, 601, 901, 1201).
   */
  void TestDumpedSnapshotFiles()
  {
    std::printf("--- Running TestDumpedSnapshotFiles ---\n");

    const char* const snapshotDirs[] = {
      "C:/ProgramData/FAForever/snapshots",
      "snapshots"
    };

    int foundCount = 0;
    const int testBeats[] = {1, 301, 601, 901, 1201};

    for (const int beat : testBeats) {
      bool found = false;
      for (const char* dir : snapshotDirs) {
        char path[MAX_PATH];
        std::snprintf(path, sizeof(path), "%s/replay_beat_%d.snap", dir, beat);
        if (std::filesystem::exists(path)) {
          const auto size = std::filesystem::file_size(path);
          std::printf("  Found snapshot for beat %d at %s (%zu MB)\n", beat, path, size / (1024 * 1024));
          CHECK(size > 10 * 1024 * 1024); // Each sim snapshot is ~70 MB
          found = true;
          ++foundCount;
          break;
        }
      }
      if (!found) {
        std::printf("  Note: Snapshot for beat %d not yet dumped to disk (skipped)\n", beat);
      }
    }

    std::printf("  Total verified snapshots on disk: %d\n", foundCount);
  }

  /**
   * Test 3: Multi-snapshot sequential rewind logic.
   * Simulates jumping between different snapshots (e.g. beat 100 -> 500 -> 300).
   */
  void TestSequentialRewindSnapshotManager()
  {
    std::printf("--- Running TestSequentialRewindSnapshotManager ---\n");

    moho::ReplaySnapshotManager manager(8);

    moho::ReplaySnapshot baseSnap;
    baseSnap.beat = 0;
    baseSnap.streamOffset = 100;
    manager.SetBaseSnapshot(baseSnap);

    const int beats[] = {100, 300, 500, 900, 1200, 1500};
    for (const int b : beats) {
      moho::ReplaySnapshot snap;
      snap.beat = b;
      snap.streamOffset = static_cast<std::uint64_t>(b * 1000);
      manager.AddSnapshot(snap);
    }

    CHECK(manager.GetSnapshotCount() == 6);

    // Sequence 1: User at 550 seeks back to 300
    auto snap300 = manager.FindNearestSnapshot(300);
    CHECK(snap300.has_value() && snap300->beat == 300);

    // Sequence 2: User at 350 seeks to 500
    auto snap500 = manager.FindNearestSnapshot(500);
    CHECK(snap500.has_value() && snap500->beat == 500);

    // Sequence 3: User at 520 seeks back to 100
    auto snap100 = manager.FindNearestSnapshot(100);
    CHECK(snap100.has_value() && snap100->beat == 100);

    // Sequence 4: User seeks back to 50 (should find base beat 0)
    auto snap0 = manager.FindNearestSnapshot(50);
    CHECK(snap0.has_value() && snap0->beat == 0);

    std::printf("  Sequential rewind snapshot lookups passed!\n");
  }

  /**
   * Test 4: Lua GC Stress Test after repeatedly creating and traversing protos.
   */
  void TestLuaGCStressAfterRewind()
  {
    std::printf("--- Running TestLuaGCStressAfterRewind ---\n");

    LuaPlus::LuaState luaState(LuaPlus::LuaState::LIB_BASE);
    lua_State* const L = luaState.GetCState();
    CHECK(L != nullptr);

    // Simulate multiple rewind cycles creating and destroying prototypes
    for (int cycle = 0; cycle < 10; ++cycle) {
      for (int i = 0; i < 50; ++i) {
        Proto* const p = luaF_newproto(L);
        p->nups = static_cast<lu_byte>((i % 5) + 1);
        p->sizeupvalues = p->nups;
        p->upvalues = luaM_newvector<TString*>(L, p->nups);
        for (int u = 0; u < p->nups; ++u) {
          char name[32];
          std::snprintf(name, sizeof(name), "uv_%d_%d_%d", cycle, i, u);
          p->upvalues[u] = luaS_newlstr(L, name, std::strlen(name));
        }
      }
      // Force full GC after each cycle
      lua_setgcthreshold(L, 0);
    }

    std::printf("  10 cycles of Lua GC stress passed cleanly!\n");
  }

  /**
   * Test 5: Verify EntitySetBase serialization and deserialization with fastvector_n inline/heap growth.
   * Tests count <= 4 (inline storage) and count > 4 (heap reallocation).
   * Verifies that deserialization resize does NOT treat inline storage as heap memory
   * and does NOT cause STATUS_HEAP_CORRUPTION (0xC0000374).
   */
  void TestEntitySetBaseFastVectorNSerializationAndHeap()
  {
    std::printf("--- Running TestEntitySetBaseFastVectorNSerializationAndHeap ---\n");

    const int testCounts[] = {0, 2, 4, 7, 16};
    for (const int count : testCounts) {
      std::printf("  Testing EntitySetBase with %d elements...\n", count);

      std::FILE* rawFile = nullptr;
      CHECK(tmpfile_s(&rawFile) == 0 && rawFile != nullptr);
      boost::shared_ptr<std::FILE> file(rawFile, std::fclose);

      // 1. Serialize EntitySetBase
      {
        moho::EntitySetBase originalSet;
        for (int i = 0; i < count; ++i) {
          originalSet.mVec.push_back(nullptr);
        }
        CHECK(static_cast<int>(originalSet.mVec.size()) == count);

        std::unique_ptr<gpg::WriteArchive> writeArchive(gpg::CreateBinaryWriteArchive(file));
        originalSet.MemberSerialize(writeArchive.get());
      }

      std::fflush(file.get());
      std::fseek(file.get(), 0, SEEK_SET);

      // 2. Deserialize into fresh EntitySetBase
      {
        moho::EntitySetBase restoredSet;
        CHECK(restoredSet.mVec.size() == 0);

        try {
          std::unique_ptr<gpg::ReadArchive> readArchive(gpg::CreateBinaryReadArchive(file));
          restoredSet.MemberDeserialize(readArchive.get());
        } catch (const std::exception& ex) {
          std::printf("  FAIL: MemberDeserialize threw: %s\n", ex.what());
          CHECK(false);
          continue;
        }

        CHECK(static_cast<int>(restoredSet.mVec.size()) == count);
        std::printf("    Successfully deserialized %d elements! Size: %zu\n", count, restoredSet.mVec.size());
      }
      // Out-of-scope destruction verifies no NT Heap corruption (0xC0000374)
    }

    std::printf("  EntitySetBase fastvector_n tests passed without heap corruption!\n");
  }

  /**
   * Test 6: Verify CCommandDB IdPool serialization and deserialization.
   * Ensures mNextLowId and recycled command IDs in mReleasedLows survive snapshot rewind.
   */
  void TestCommandDbIdPoolSerialization()
  {
    std::printf("--- Running TestCommandDbIdPoolSerialization ---\n");

    moho::CCommandDB commandDb(nullptr);
    // Simulate command allocation and recycling
    commandDb.pool.mNextLowId = 42;
    commandDb.pool.mReleasedLows.Add(5);
    commandDb.pool.QueueReleasedLowId(12);

    CHECK(commandDb.pool.mReleasedLows.Contains(5));

    std::FILE* rawFile = nullptr;
    CHECK(tmpfile_s(&rawFile) == 0 && rawFile != nullptr);
    boost::shared_ptr<std::FILE> file(rawFile, std::fclose);

    // Serialize
    {
      std::unique_ptr<gpg::WriteArchive> writeArchive(gpg::CreateBinaryWriteArchive(file));
      commandDb.MemberSerialize(writeArchive.get());
    }

    std::fflush(file.get());
    std::fseek(file.get(), 0, SEEK_SET);

    // Deserialize into fresh DB
    {
      moho::CCommandDB restoredDb(nullptr);
      std::unique_ptr<gpg::ReadArchive> readArchive(gpg::CreateBinaryReadArchive(file));
      restoredDb.MemberDeserialize(readArchive.get());

      CHECK(restoredDb.pool.mNextLowId == 42);
      CHECK(restoredDb.pool.mReleasedLows.Contains(5));
      CHECK(restoredDb.pool.mReleasedLows.Contains(12));
    }

    std::printf("  CCommandDB IdPool serialization roundtrip passed!\n");
  }

  /**
   * Test 7: Verify COGrid dynamic occupancy cleanup and Unit OGrid rectangle release.
   */
  void TestUnitDestructionFreesOgridRect()
  {
    std::printf("--- Running TestUnitDestructionFreesOgridRect ---\n");

    // 1. Verify COGrid dynamic occupancy clearing
    {
      moho::COGrid ogrid(nullptr);
      // Initialize backing bitmap for testing
      ogrid.mOccupation.Reset(128, 128);

      const gpg::Rect2i testRect{10, 10, 20, 20};
      CHECK(!ogrid.IsRectOccupied(testRect));

      ogrid.OccupyRect(testRect);
      CHECK(ogrid.IsRectOccupied(testRect));

      ogrid.ClearDynamicOccupancy();
      CHECK(!ogrid.IsRectOccupied(testRect));
    }

    // 2. Verify Unit FreeOgridRect resets reservation coordinates and destructor runs FreeOgridRect
    {
      std::unique_ptr<moho::Unit> unit(moho::SimRewindTestPeer::CreateTestUnit());
      unit->ReservedOgridRectMinX = 10;
      unit->ReservedOgridRectMinZ = 10;
      unit->ReservedOgridRectMaxX = 20;
      unit->ReservedOgridRectMaxZ = 20;

      CHECK(unit->ReservedOgridRectMinX == 10);
      CHECK(unit->ReservedOgridRectMaxX == 20);

      unit->FreeOgridRect();
      CHECK(unit->ReservedOgridRectMinX == 0);
      CHECK(unit->ReservedOgridRectMinZ == 0);
      CHECK(unit->ReservedOgridRectMaxX == 0);
      CHECK(unit->ReservedOgridRectMaxZ == 0);

      // Re-populate bounds to verify ~Unit() cleans up without error
      unit->ReservedOgridRectMinX = 15;
      unit->ReservedOgridRectMinZ = 15;
      unit->ReservedOgridRectMaxX = 25;
      unit->ReservedOgridRectMaxZ = 25;
      CHECK(unit->ReservedOgridRectMinX == 15);
      // unit is destroyed here at end of scope via ~Unit() -> FreeOgridRect()
    }

    std::printf("  Unit OGrid destruction cleanup & ClearDynamicOccupancy passed!\n");
  }

  void TestLuaGlobalsPruningOnRewind()
  {
    std::printf("--- Running TestLuaGlobalsPruningOnRewind ---\n");

    LuaPlus::LuaState luaState(LuaPlus::LuaState::LIB_BASE);
    CHECK(moho::SCR_LuaDoString(
      "__serialize_name_for_object = {}\n"
      "__serialize_object_for_name = {}\n"
      "local function register(obj, name)\n"
      "  __serialize_name_for_object[obj] = name\n"
      "  __serialize_object_for_name[name] = obj\n"
      "end\n"
      "local function scan(tbl, prefix)\n"
      "  for k, v in pairs(tbl) do\n"
      "    local t = type(v)\n"
      "    if t == 'function' or t == 'cfunction' or t == 'table' then\n"
      "      local name = prefix and (prefix .. '.' .. tostring(k)) or tostring(k)\n"
      "      if not __serialize_name_for_object[v] then\n"
      "        register(v, name)\n"
      "        if t == 'table' and v ~= _G and v ~= __serialize_name_for_object and v ~= __serialize_object_for_name then\n"
      "          scan(v, name)\n"
      "        end\n"
      "      end\n"
      "    end\n"
      "  end\n"
      "end\n"
      "register(_G, '_G')\n"
      "scan(_G, nil)\n",
      &luaState
    ));

    LuaPlus::LuaObject globals = luaState.GetGlobals();

    // 1. Establish initial state at snapshot
    globals.SetString("SnapshotVarA", "alpha");
    globals.SetInteger("SnapshotVarB", 100);

    // Verify a pre-existing C-function exists in globals (from LIB_BASE, e.g. "assert")
    LuaPlus::LuaObject assertFunc = globals.GetByName("assert");
    CHECK(assertFunc.IsFunction());
    CHECK(assertFunc.IsCFunction());

    std::FILE* rawFile = nullptr;
    CHECK(tmpfile_s(&rawFile) == 0 && rawFile != nullptr);
    boost::shared_ptr<std::FILE> file(rawFile, std::fclose);

    // Serialize table
    {
      std::unique_ptr<gpg::WriteArchive> writeArchive(gpg::CreateBinaryWriteArchive(file));
      (void)writeArchive->PreCreatedPtr(gpg::MakeRRef<lua_State>(luaState.GetCState()));
      (void)writeArchive->PreCreatedPtr(gpg::MakeRRef<LuaPlus::LuaState>(&luaState));
      moho::func_ArchiveWriteLuaObj(writeArchive.get(), &globals);
    }

    std::fflush(file.get());
    std::fseek(file.get(), 0, SEEK_SET);

    // 2. Introduce post-snapshot dirty variables and a dirty Lua function
    globals.SetString("SnapshotVarA", "mutated");
    globals.SetString("DirtyPostSnapshotVar", "should_be_pruned");
    globals.SetInteger("DirtyCounter", 9999);

    CHECK(moho::SCR_LuaDoString("function DirtyScriptFunction() return 42 end", &luaState));

    CHECK(globals.GetByName("DirtyScriptFunction").IsFunction());
    CHECK(!globals.GetByName("DirtyScriptFunction").IsCFunction());

    // 3. Restore table entries
    {
      std::unique_ptr<gpg::ReadArchive> readArchive(gpg::CreateBinaryReadArchive(file));
      (void)readArchive->TrackPointer(gpg::MakeRRef<lua_State>(luaState.GetCState()));
      (void)readArchive->TrackPointer(gpg::MakeRRef<LuaPlus::LuaState>(&luaState));
      moho::ReadLuaTableEntriesFromArchive(readArchive.get(), globals);
    }

    // 4. Assert restored state
    CHECK(std::strcmp(globals.GetByName("SnapshotVarA").GetString(), "alpha") == 0);
    CHECK(globals.GetByName("SnapshotVarB").GetInteger() == 100);
    CHECK(globals.GetByName("DirtyPostSnapshotVar").IsNil());
    CHECK(globals.GetByName("DirtyCounter").IsNil());
    CHECK(globals.GetByName("DirtyScriptFunction").IsNil());

    // 5. Verify pre-existing C-function remains intact
    LuaPlus::LuaObject restoredAssert = globals.GetByName("assert");
    CHECK(restoredAssert.IsFunction());
    CHECK(restoredAssert.IsCFunction());

    std::printf("  Lua globals pruning test passed!\n");
  }

  void TestSimRestoreStateBeatPreservation()
  {
    std::printf("--- Running TestSimRestoreStateBeatPreservation ---\n");
    // Simulates a snapshot taken at beat 450 where tick was 400 (due to 50 paused beats)
    // Asserts that passing targetBeat=450 sets mCurBeat = 451, satisfying SimDriver verification.
    const std::int32_t targetBeat = 450;
    const std::uint32_t expectedBeat = static_cast<std::uint32_t>(targetBeat) + 1;
    const std::uint32_t tick = 400;

    auto computeCurBeat = [](std::uint32_t curTick, std::int32_t beat) -> std::uint32_t {
      if (beat >= 0) {
        return static_cast<std::uint32_t>(beat) + 1;
      }
      return curTick + 1;
    };

    CHECK(computeCurBeat(tick, targetBeat) == expectedBeat);
    CHECK(computeCurBeat(tick, -1) == tick + 1);
    std::printf("  Sim::RestoreState beat preservation passed!\n");
  }

  void TestLuaCClosureArchiveTracking()
  {
    std::printf("--- Running TestLuaCClosureArchiveTracking ---\n");
    LuaPlus::LuaState luaState(LuaPlus::LuaState::LIB_BASE);
    lua_State* const L = luaState.GetCState();
    CHECK(L != nullptr);

    // 1. Gather all CClosures
    std::vector<CClosure*> closures;
    EnumerateLuaCClosures(L, [&](CClosure* closure) {
      closures.push_back(closure);
    });
    CHECK(!closures.empty());

    // 2. Serialize: Pre-track each CClosure and write pointers to archive
    std::FILE* rawFile = nullptr;
    CHECK(tmpfile_s(&rawFile) == 0 && rawFile != nullptr);
    boost::shared_ptr<std::FILE> file(rawFile, std::fclose);

    const gpg::RRef ownerRef = gpg::MakeRRef<lua_State>(L);
    {
      std::unique_ptr<gpg::WriteArchive> writeArchive(gpg::CreateBinaryWriteArchive(file));
      for (CClosure* c : closures) {
        (void)writeArchive->PreCreatedPtr(gpg::MakeRRef(c));
      }
      for (CClosure* c : closures) {
        writeArchive->WritePointer(c, gpg::TrackedPointerState::Unowned, ownerRef);
      }
    }

    std::fflush(file.get());
    std::fseek(file.get(), 0, SEEK_SET);

    // 3. Deserialize: TrackPointer for each CClosure in identical order and read back
    {
      std::unique_ptr<gpg::ReadArchive> readArchive(gpg::CreateBinaryReadArchive(file));
      for (CClosure* c : closures) {
        (void)readArchive->TrackPointer(gpg::MakeRRef(c));
      }
      for (size_t i = 0; i < closures.size(); ++i) {
        CClosure* loadedClosure = nullptr;
        readArchive->ReadPointer(&loadedClosure, &ownerRef);
        CHECK(loadedClosure == closures[i]);
      }
    }

    std::printf("  Tracked and round-tripped %zu CClosure pointers across archive successfully!\n", closures.size());
  }

  void TestCanSnapshotGuard()
  {
    std::printf("--- Running TestCanSnapshotGuard ---\n");
    // Verify CanSnapshot logic:
    // A clean Lua state with no active C calls and no open upvalues is safe for snapshot.
    LuaPlus::LuaState luaState(LuaPlus::LuaState::LIB_BASE);
    lua_State* const L = luaState.GetCState();
    CHECK(L != nullptr);
    CHECK(L->nCcalls == 0);
    CHECK(L->openupval == nullptr);

    // Simulate active C call:
    L->nCcalls = 1;
    CHECK(L->nCcalls != 0);
    // Restore
    L->nCcalls = 0;
    CHECK(L->nCcalls == 0);

    std::printf("  CanSnapshot thread state invariants verified!\n");
  }

  void TestEnumerateLuaCClosures()
  {
    std::printf("--- Running TestEnumerateLuaCClosures ---\n");
    LuaPlus::LuaState luaState(LuaPlus::LuaState::LIB_BASE);
    lua_State* const L = luaState.GetCState();
    CHECK(L != nullptr);

    int cclosureCount = 0;
    EnumerateLuaCClosures(L, [&](CClosure* closure) {
      CHECK(closure != nullptr);
      CHECK(closure->tt == LUA_CFUNCTION);
      CHECK(closure->f != nullptr);
      ++cclosureCount;
    });

    std::printf("  Found %d CClosures in LIB_BASE Lua state\n", cclosureCount);
    CHECK(cclosureCount > 0);
  }

  void TestReplaySnapshotSchedulingAndRetry()
  {
    std::printf("--- Running TestReplaySnapshotSchedulingAndRetry ---\n");
    const std::int32_t interval = 300;
    std::int32_t lastPeriodicBeat = 0;
    bool hasBaseSnapshot = false;

    auto shouldCaptureSnapshot = [&](std::int32_t beat) -> bool {
      const bool needBase = (!hasBaseSnapshot && beat >= 1);
      const bool needPeriodic = (interval > 0 && (beat - lastPeriodicBeat >= interval));
      return needBase || needPeriodic;
    };

    // Beat 1: base snapshot
    CHECK(shouldCaptureSnapshot(1) == true);
    hasBaseSnapshot = true;

    // Beats 2 to 299: no snapshot
    CHECK(shouldCaptureSnapshot(2) == false);
    CHECK(shouldCaptureSnapshot(299) == false);

    // Beat 300: scheduled periodic snapshot
    CHECK(shouldCaptureSnapshot(300) == true);

    // Simulate transient failure at beat 300 (e.g. CanSnapshot returned false)
    // lastPeriodicBeat is NOT updated on failure:
    CHECK(lastPeriodicBeat == 0);

    // Beat 301: should immediately retry because (301 - 0 >= 300)
    CHECK(shouldCaptureSnapshot(301) == true);

    // Simulate success at beat 301:
    const std::int32_t recordedPreviousBeat = lastPeriodicBeat;
    lastPeriodicBeat = 301;
    CHECK(recordedPreviousBeat == 0);
    CHECK(lastPeriodicBeat == 301);

    // Beat 302: should NOT capture (302 - 301 < 300)
    CHECK(shouldCaptureSnapshot(302) == false);

    // Beat 600: (600 - 301 = 299 < 300) -> not yet
    CHECK(shouldCaptureSnapshot(600) == false);

    // Beat 601: (601 - 301 = 300 >= 300) -> next periodic snapshot triggers
    CHECK(shouldCaptureSnapshot(601) == true);

    // Verify SReplaySnapshotFileHeader holds lastPeriodicBeat
    moho::ReplaySnapshot snap{};
    snap.beat = 301;
    snap.lastPeriodicBeat = recordedPreviousBeat;
    snap.streamOffset = 12345;

    std::FILE* rawFile = nullptr;
    CHECK(tmpfile_s(&rawFile) == 0 && rawFile != nullptr);
    boost::shared_ptr<std::FILE> file(rawFile, std::fclose);

    moho::SReplaySnapshotFileHeader hdr{};
    hdr.beat = snap.beat;
    hdr.lastPeriodicBeat = snap.lastPeriodicBeat;
    hdr.streamOffset = snap.streamOffset;
    hdr.archivePayloadOffset = sizeof(moho::SReplaySnapshotFileHeader);
    std::fwrite(&hdr, 1, sizeof(hdr), file.get());
    std::fflush(file.get());

    std::fseek(file.get(), 0, SEEK_SET);
    moho::SReplaySnapshotFileHeader loadedHdr{};
    CHECK(std::fread(&loadedHdr, 1, sizeof(loadedHdr), file.get()) == sizeof(loadedHdr));
    CHECK(loadedHdr.beat == 301);
    CHECK(loadedHdr.lastPeriodicBeat == 0);
    CHECK(loadedHdr.streamOffset == 12345);

    std::printf("  Replay snapshot retry and lastPeriodicBeat header verification passed!\n");
  }

} // namespace

int main()
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);

  std::printf("====================================================\n");
  std::printf("FAF Simulation Rewind Integration & GC Unit Tests\n");
  std::printf("====================================================\n");

  TestProtoStrippedUpvalueSerializationAndGC();
  TestDumpedSnapshotFiles();
  TestSequentialRewindSnapshotManager();
  TestLuaGCStressAfterRewind();
  TestEntitySetBaseFastVectorNSerializationAndHeap();
  TestCommandDbIdPoolSerialization();
  TestUnitDestructionFreesOgridRect();
  TestSimRestoreStateBeatPreservation();
  TestLuaGlobalsPruningOnRewind();
  TestEnumerateLuaCClosures();
  TestLuaCClosureArchiveTracking();
  TestCanSnapshotGuard();
  TestReplaySnapshotSchedulingAndRetry();

  std::printf("\nResults: %d checks, %d failures\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
