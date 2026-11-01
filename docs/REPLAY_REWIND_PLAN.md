# Replay Rewind Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement bidirectional replay time scrubbing (rewind backwards in time and fast-forward) in FAF-RE using in-memory periodic snapshots, stream rewinding, and state restoration with 100% bit-for-bit determinism.

**Architecture:** A lightweight in-memory `ReplaySnapshotManager` records a permanent base snapshot at Beat 0 and maintains a sliding ring buffer of periodic snapshots during replay playback. When seeking backwards, the engine locates the nearest checkpoint $S \le targetBeat$, rolls back `Sim` state, rewinds `CSimDriver` queue and `CReplayClient` stream offset to $S$, and turbo-scrubs forward from $S$ to $targetBeat$.

**Tech Stack:** C++20, CMake, Moho Engine (`Sim`, `CWldSession`, `CSimDriver`, `CReplayClient`), Lua 5.0 (SupCom UI), Catch2 / CTest.

**Spec:** [docs/REPLAY_REWIND_DESIGN.md](file:///i:/ProjectsFaf/faf-re/docs/REPLAY_REWIND_DESIGN.md)

## Global Constraints

- 100% Bit-for-Bit Determinism: all MD5 hashes of simulation state after rewind must match original playback bit-for-bit.
- Zero disk I/O for snapshots: snapshots stored in RAM via byte buffers / `PipeStream`.
- Zero overhead for live games: snapshot manager active only when `IsReplay == true`.
- Workspace rule: all new console variables/switches must be documented in `docs/SWITCHES.md`.

## Review Focus

1. **User seeks backwards to Beat 0**: Ensure `mBaseSnapshot` successfully restores initial game state and rewinds stream to byte offset of Beat 1.
2. **User drags slider rapidly back and forth**: Ensure in-flight scrub cancellations cleanly abort and reset without hanging `SessionFrame`.
3. **User seeks to exact tick of a snapshot ($targetBeat == S.beat$)**: Ensure scrubbing completes immediately without executing extraneous advance packets.
4. **Ring buffer eviction**: Ensure old snapshot memory is completely deallocated when new snapshots exceed `sim_ReplaySnapshotMax`.
5. **Selection and UI synchronization**: Ensure selecting units before rewind does not leave dangling pointers or crash Lua UI when state rolls back.

---

### Task 1: `ReplaySnapshotManager` Core Architecture (In-Memory Snapshots)

**Files:**
- Create: `src/moho/sim/ReplaySnapshotManager.h`
- Create: `src/moho/sim/ReplaySnapshotManager.cpp`
- Test: `tests/moho/ReplaySnapshotManagerTests.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  namespace moho {
    struct ReplaySnapshot {
      std::int32_t beat{0};
      std::uint64_t streamOffset{0};
      gpg::MD5Digest simHash{};
      std::vector<std::uint8_t> data{};
    };

    class ReplaySnapshotManager {
    public:
      explicit ReplaySnapshotManager(std::size_t maxSnapshots = 8);
      void SetBaseSnapshot(ReplaySnapshot snapshot);
      const ReplaySnapshot* GetBaseSnapshot() const noexcept;
      void AddSnapshot(ReplaySnapshot snapshot);
      [[nodiscard]] const ReplaySnapshot* FindNearestSnapshot(std::int32_t targetBeat) const noexcept;
      [[nodiscard]] std::size_t GetSnapshotCount() const noexcept;
      void SetMaxSnapshots(std::size_t maxSnapshots) noexcept;
      void Clear() noexcept;
    private:
      std::size_t mMaxSnapshots{8};
      std::optional<ReplaySnapshot> mBaseSnapshot{};
      std::deque<ReplaySnapshot> mRingBuffer{};
    };
  }
  ```

- [ ] **Step 1: Write failing test for ReplaySnapshotManager**

Create `tests/moho/ReplaySnapshotManagerTests.cpp`:
```cpp
#include "moho/sim/ReplaySnapshotManager.h"
#include <cassert>
#include <cstdio>

int main() {
  moho::ReplaySnapshotManager manager(3);
  assert(manager.GetSnapshotCount() == 0);
  assert(manager.FindNearestSnapshot(100) == nullptr);

  // Set base snapshot at beat 0
  moho::ReplaySnapshot baseSnap;
  baseSnap.beat = 0;
  baseSnap.streamOffset = 1000;
  manager.SetBaseSnapshot(baseSnap);
  assert(manager.GetBaseSnapshot() != nullptr);
  assert(manager.GetBaseSnapshot()->beat == 0);

  // When ring buffer is empty, FindNearestSnapshot returns base snapshot
  const auto* snap = manager.FindNearestSnapshot(150);
  assert(snap != nullptr && snap->beat == 0);

  // Add periodic snapshots
  moho::ReplaySnapshot s1; s1.beat = 300; s1.streamOffset = 2000;
  moho::ReplaySnapshot s2; s2.beat = 600; s2.streamOffset = 3000;
  moho::ReplaySnapshot s3; s3.beat = 900; s3.streamOffset = 4000;
  manager.AddSnapshot(s1);
  manager.AddSnapshot(s2);
  manager.AddSnapshot(s3);
  assert(manager.GetSnapshotCount() == 3);

  // Lookup tests
  assert(manager.FindNearestSnapshot(200)->beat == 0);
  assert(manager.FindNearestSnapshot(300)->beat == 300);
  assert(manager.FindNearestSnapshot(599)->beat == 300);
  assert(manager.FindNearestSnapshot(600)->beat == 600);
  assert(manager.FindNearestSnapshot(1000)->beat == 900);

  // Eviction test: adding 4th snapshot evicts s1 (beat 300)
  moho::ReplaySnapshot s4; s4.beat = 1200; s4.streamOffset = 5000;
  manager.AddSnapshot(s4);
  assert(manager.GetSnapshotCount() == 3);
  assert(manager.FindNearestSnapshot(400)->beat == 0); // s1 was evicted, falls back to base

  std::printf("ReplaySnapshotManager tests passed!\n");
  return 0;
}
```

- [ ] **Step 2: Add test target in tests/CMakeLists.txt and verify it fails**

Run: `cmake --build --preset clang-x64-release --target faf_replay_snapshot_tests`
Expected: FAIL (missing header/source)

- [ ] **Step 3: Implement ReplaySnapshotManager**

Create `src/moho/sim/ReplaySnapshotManager.h` and `src/moho/sim/ReplaySnapshotManager.cpp`, register in `src/CMakeLists.txt`.

- [ ] **Step 4: Build and run test**

Run: `ctest --test-dir build/clang-x64-release -R replay_snapshot`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/moho/sim/ReplaySnapshotManager.* tests/moho/ReplaySnapshotManagerTests.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(sim): add in-memory ReplaySnapshotManager"
```

---

### Task 2: `Sim` Serialization & In-Memory Capture/Restore State

**Files:**
- Modify: `src/moho/sim/Sim.h`
- Modify: `src/moho/sim/Sim.cpp`
- Modify: `src/moho/entity/SSTIEntityVariableData.cpp`
- Modify: `src/moho/net/CClientManagerImpl.h`
- Modify: `src/moho/net/CClientManagerImpl.cpp`
- Modify: `src/gpg/core/streams/PipeStream.h`
- Modify: `src/gpg/core/streams/PipeStream.cpp`

**Interfaces:**
- Consumes: `moho::Sim`, `gpg::WriteArchive`, `gpg::ReadArchive`
- Produces:
  ```cpp
  namespace moho {
    class Sim {
    public:
      void SaveState(gpg::WriteArchive* archive);
      void RestoreState(gpg::ReadArchive* archive);
    };
  }
  ```

- [ ] **Step 1: Port tested serialization fixes from feature/replay-rewind**

Apply `SSTIEntityVariableData.cpp` attach info RType resolution, `Sim::RestoreState` teardown and context resetting, and `PipeStream::GetData` helper.

- [ ] **Step 2: Implement Sim::SaveState and Sim::RestoreState in Sim.cpp**

Ensure task stages `mTaskStageA/B` teardown, unlinking `mCoordEntities`, clearing `mCommandDB`, and deserializing into `this`.

- [ ] **Step 3: Compile engine to verify clean build**

Run: `cmake --build --preset clang-x64-release --target faf_engine`
Expected: Build passes with 0 errors.

- [ ] **Step 4: Commit**

```bash
git add src/moho/sim/Sim.* src/moho/entity/SSTIEntityVariableData.cpp src/gpg/core/streams/PipeStream.*
git commit -m "feat(sim): implement Sim SaveState and RestoreState for rewind"
```

---

### Task 3: Stream and Driver Rewind Plumbing (`CReplayClient`, `CSimDriver`)

**Files:**
- Modify: `src/moho/net/CReplayClient.h`
- Modify: `src/moho/net/CReplayClient.cpp`
- Modify: `src/moho/sim/SimDriver.h`
- Modify: `src/moho/sim/SimDriver.cpp`

**Interfaces:**
- Produces:
  ```cpp
  class CReplayClient {
  public:
    [[nodiscard]] std::uint64_t GetStreamPosition() const;
    [[nodiscard]] std::uint64_t GetBeatStreamPosition(std::int32_t beat) const;
    void RewindStream(std::uint64_t pos, std::int32_t beat);
  };

  class CSimDriver {
  public:
    void RewindBeats(int32_t beat);
    void RewindBeatsLocked(int32_t beat);
  };
  ```

- [ ] **Step 1: Implement beat stream offset tracking in CReplayClient**

Track stream positions at each beat dispatch in `sBeatStreamOffsets`, implement `GetBeatStreamPosition` and `RewindStream`.

- [ ] **Step 2: Implement RewindBeatsLocked in CSimDriver**

Reset `mDispatchBeat = beat + 1`, `mNextIssueBeat = beat + 1`, `mLastDequeuedBeat = beat`, and clear `mSyncDataQueue.ClearAndDelete()`.

- [ ] **Step 3: Compile and verify unit builds**

Run: `cmake --build --preset clang-x64-release --target faf_engine`
Expected: Build passes.

- [ ] **Step 4: Commit**

```bash
git add src/moho/net/CReplayClient.* src/moho/sim/SimDriver.*
git commit -m "feat(net): add stream and queue rewind plumbing to CReplayClient and CSimDriver"
```

---

### Task 4: `CWldSession` Rewind Pipeline & Snapshot Lifecycle

**Files:**
- Modify: `src/moho/sim/CWldSession.h`
- Modify: `src/moho/sim/CWldSession.cpp`
- Modify: `docs/SWITCHES.md`

**Interfaces:**
- Modifies `CWldSession::StartScrubToBeat(targetBeat)` to support $targetBeat < mGameTick$.
- Exposes convars `sim_ReplaySnapshotInterval` and `sim_ReplaySnapshotMax`.

- [ ] **Step 1: Add ReplaySnapshotManager to CWldSession**

Instantiate `std::unique_ptr<ReplaySnapshotManager> mSnapshotManager` in `CWldSession`. Capture `mBaseSnapshot` at initial beat 0.

- [ ] **Step 2: Implement periodic snapshot capture in SessionFrame**

When `IsReplay` and `!mIsScrubbing`, capture a snapshot every `sim_ReplaySnapshotInterval` beats into memory.

- [ ] **Step 3: Update StartScrubToBeat for backwards seek**

If $targetBeat < mGameTick$:
1. Locate nearest snapshot $S = \text{FindNearestSnapshot}(targetBeat)$.
2. Set `mIsScrubbing = true`, `mScrubStartBeat = S->beat`, `mScrubTargetBeat = targetBeat`.
3. Call `RewindBeatsLocked(S->beat)` on driver, `RewindStream(S->streamOffset, S->beat)` on client.
4. Call `Sim::RestoreState` with $S->data$.
5. Update `mGameTick = S->beat`.
6. If $targetBeat == S->beat$, complete scrubbing immediately. Otherwise, allow turbo forward loop to advance to $targetBeat$.

- [ ] **Step 4: Register and document convars**

Add `sim_ReplaySnapshotInterval` and `sim_ReplaySnapshotMax` in `CWldSession.cpp` and [docs/SWITCHES.md](file:///i:/ProjectsFaf/faf-re/docs/SWITCHES.md).

- [ ] **Step 5: Commit**

```bash
git add src/moho/sim/CWldSession.* docs/SWITCHES.md
git commit -m "feat(sim): integrate ReplaySnapshotManager and rewind pipeline into CWldSession"
```

---

### Task 5: User/UI Layer Integration & Timeline Lua Hook

**Files:**
- Modify: `data/fafengine/lua/ui/game/replay_timeline.lua`
- Modify: `src/moho/sim/UserLuaFunctions.cpp`

**Interfaces:**
- Lua API: `ReplaySeek(targetBeat)` accepts backward ticks.
- UI: Replay timeline slider allows dragging backwards.

- [ ] **Step 1: Update replay_timeline.lua slider event handler**

Remove backward seek restriction. When user drags backward, invoke `ReplaySeek(targetBeat)`.

- [ ] **Step 2: Add ClearSelection trigger on rewind completion**

Ensure stale unit selections from future beats are cleared when rewind finishes.

- [ ] **Step 3: Commit**

```bash
git add data/fafengine/lua/ui/game/replay_timeline.lua src/moho/sim/UserLuaFunctions.cpp
git commit -m "feat(ui): enable backward timeline scrubbing and selection reset"
```

---

### Task 6: Comprehensive Automated Tests (Forward & Backward Parity)

**Files:**
- Modify: `tests/moho/ReplaySetupTests.cpp`

**Interfaces:**
- Tests bidirectional scrubbing and multi-pass determinism.

- [ ] **Step 1: Write TestReplayScrubbingForwardAndBackwardParity test**

In `tests/moho/ReplaySetupTests.cpp`:
1. Start replay session from beat 0.
2. Scrub forward to beat 500 (`StartScrubToBeat(500)`). Record hashes from beat 200 to 500.
3. Scrub backward to beat 200 (`StartScrubToBeat(200)`).
4. Run forward to beat 500.
5. Verify bit-for-bit MD5 digest parity between first and second pass for all beats 200..500.

- [ ] **Step 2: Write TestReplayScrubbingToBaseBeat0 test**

Scrub deep into replay (beat 800), scrub back to 0, verify game state is fully intact and matches beat 0 golden hash.

- [ ] **Step 3: Write TestReplayRapidScrubbing test**

Perform rapid sequence: 0 -> 600 -> 250 -> 500 -> 100 -> 400 without crashes.

- [ ] **Step 4: Run test suite**

Run: `ctest --test-dir build/clang-x64-release -R replay_setup`
Expected: All tests PASS.

- [ ] **Step 5: Commit**

```bash
git add tests/moho/ReplaySetupTests.cpp
git commit -m "test(sim): add bidirectional forward and backward replay scrubbing parity tests"
```
