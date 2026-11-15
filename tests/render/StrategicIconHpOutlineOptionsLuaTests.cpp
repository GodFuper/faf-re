#include <gtest/gtest.h>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "lua/LuaObject.h"
#include "lua/LuaPrimitives.h"
#include "moho/sim/CWldSession.h"
#include "moho/misc/StartupHelpers.h"
#include "moho/lua/CScrLuaInitForm.h"

namespace
{
  std::filesystem::path FindOptionsLua()
  {
    std::error_code ec;

    // Check environment variable
    if (const char* envPath = std::getenv("FAF_FA_ROOT")) {
      const std::filesystem::path candidate = std::filesystem::path(envPath) / "lua" / "options" / "options.lua";
      if (std::filesystem::exists(candidate, ec)) {
        return candidate;
      }
    }

    // Direct well-known absolute path
    const std::filesystem::path wellKnown = "I:/ProjectsFaf/fa/lua/options/options.lua";
    if (std::filesystem::exists(wellKnown, ec)) {
      return wellKnown;
    }

    // Relative candidates from current working directory
    const std::filesystem::path cur = std::filesystem::current_path(ec);
    for (const auto& rel : {
           "../fa/lua/options/options.lua",
           "../../fa/lua/options/options.lua",
           "../../../fa/lua/options/options.lua",
           "fa/lua/options/options.lua",
           "lua/options/options.lua"
         }) {
      const std::filesystem::path candidate = cur / rel;
      if (std::filesystem::exists(candidate, ec)) {
        return std::filesystem::canonical(candidate, ec);
      }
    }

    return {};
  }

  std::string ReadFileToString(const std::filesystem::path& path)
  {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
      return {};
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
  }

  const char* kMockPrelude = R"lua(
    _ALERT = function(msg) print("[LUA_ALERT] " .. tostring(msg)) end
    import = function(p) return {} end
    HasCommandLineArg = function(a) return false end
    Sound = function(t) return t end
    PlaySound = function(s) end
    ForkThread = function(f) end
    WaitSeconds = function(s) end
    StopSound = function(h) end
    PlayVoice = function(s) end
    GetVolume = function(v) return 1.0 end
    SetVolume = function(v, val) end
    GetCurrentUIState = function() return 'game' end
    ConExecute = function(c) end
  )lua";

  class StrategicIconHpOutlineOptionsLuaTest : public ::testing::Test
  {
  protected:
    std::string mOptionsLuaSource;
    std::filesystem::path mOptionsLuaPath;

    void SetUp() override
    {
      mOptionsLuaPath = FindOptionsLua();
      ASSERT_FALSE(mOptionsLuaPath.empty()) << "Could not locate fa/lua/options/options.lua";
      mOptionsLuaSource = ReadFileToString(mOptionsLuaPath);
      ASSERT_FALSE(mOptionsLuaSource.empty()) << "options.lua was empty or could not be read: " << mOptionsLuaPath;

      // Reset native config to defaults
      moho::StrategicIconOutlineConfig def;
      moho::SetStrategicIconOutlineConfig(def);
      moho::ui_StrategicIconHpOutlinePalette = 0;
    }

    void TearDown() override
    {
      moho::StrategicIconOutlineConfig def;
      moho::SetStrategicIconOutlineConfig(def);
      moho::ui_StrategicIconHpOutlinePalette = 0;
    }

    void SetupEnvironment(LuaPlus::LuaState& state, bool registerNativeBindings)
    {
      if (registerNativeBindings) {
        (void)moho::func_SetStrategicIconOutlineConfig_LuaFuncDef();
        (void)moho::func_GetStrategicIconOutlineConfig_LuaFuncDef();
        moho::SCR_UserLuaInitSet().RunInits(&state);
      }

      lua_State* L = state.GetCState();
      int status = lua_dostring(L, kMockPrelude);
      ASSERT_EQ(status, 0) << "Failed to run mock prelude: " << lua_tostring(L, -1);

      status = lua_dostring(L, mOptionsLuaSource.c_str());
      ASSERT_EQ(status, 0) << "Failed to load/execute options.lua: " << lua_tostring(L, -1);
    }
  };
} // namespace

TEST_F(StrategicIconHpOutlineOptionsLuaTest, OptionsTableLoaded)
{
  LuaPlus::LuaState state{LuaPlus::LuaState::LIB_BASE};
  SetupEnvironment(state, true);

  const char* script = R"lua(
    assert(options ~= nil, "options table is nil")
    assert(options.ui ~= nil, "options.ui is nil")
    assert(options.ui.items ~= nil, "options.ui.items is nil")
    assert(table.getn(options.ui.items) > 0, "options.ui.items is empty")
  )lua";
  lua_State* L = state.GetCState();
  const int status = lua_dostring(L, script);
  EXPECT_EQ(status, 0) << (lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error");
}

TEST_F(StrategicIconHpOutlineOptionsLuaTest, AllFourOutlineOptionsPresentAndValid)
{
  LuaPlus::LuaState state{LuaPlus::LuaState::LIB_BASE};
  SetupEnvironment(state, true);

  const char* script = R"lua(
    local items = options.ui.items
    local byKey = {}
    for i, item in ipairs(items) do
      if item.key and item.key ~= '' then
        byKey[item.key] = item
      end
    end

    -- 1. strategic_icon_outline_mode
    local mode = byKey['strategic_icon_outline_mode']
    assert(mode ~= nil, "strategic_icon_outline_mode item not found")
    assert(mode.type == 'toggle', "mode type must be toggle")
    assert(mode.default == 0, "mode default must be 0")
    assert(type(mode.title) == 'string' and string.len(mode.title) > 0, "mode title missing")
    assert(type(mode.tip) == 'string' and string.len(mode.tip) > 0, "mode tip missing")
    assert(mode.custom ~= nil and mode.custom.states ~= nil, "mode states table missing")
    assert(table.getn(mode.custom.states) == 4, "mode must have 4 states")
    local modeKeys = {}
    for _, s in ipairs(mode.custom.states) do modeKeys[s.key] = true end
    assert(modeKeys[0] and modeKeys[1] and modeKeys[2] and modeKeys[3], "mode state keys mismatch")
    assert(type(mode.set) == 'function', "mode set must be a function")

    -- 2. strategic_icon_outline_smart_cutoff
    local cutoff = byKey['strategic_icon_outline_smart_cutoff']
    assert(cutoff ~= nil, "strategic_icon_outline_smart_cutoff item not found")
    assert(cutoff.type == 'toggle', "cutoff type must be toggle")
    assert(math.abs(cutoff.default - 1.0) < 0.001, "cutoff default must be 1.0")
    assert(type(cutoff.title) == 'string' and string.len(cutoff.title) > 0, "cutoff title missing")
    assert(type(cutoff.tip) == 'string' and string.len(cutoff.tip) > 0, "cutoff tip missing")
    assert(cutoff.custom ~= nil and cutoff.custom.states ~= nil, "cutoff states table missing")
    assert(table.getn(cutoff.custom.states) == 4, "cutoff must have 4 states")
    local cutoffKeys = {}
    for _, s in ipairs(cutoff.custom.states) do cutoffKeys[s.key] = true end
    assert(cutoffKeys[1.0] and cutoffKeys[0.95] and cutoffKeys[0.90] and cutoffKeys[0.80], "cutoff state keys mismatch")
    assert(type(cutoff.set) == 'function', "cutoff set must be a function")

    -- 3. strategic_icon_outline_shield
    local shield = byKey['strategic_icon_outline_shield']
    assert(shield ~= nil, "strategic_icon_outline_shield item not found")
    assert(shield.type == 'toggle', "shield type must be toggle")
    assert(shield.default == true, "shield default must be true")
    assert(type(shield.title) == 'string' and string.len(shield.title) > 0, "shield title missing")
    assert(type(shield.tip) == 'string' and string.len(shield.tip) > 0, "shield tip missing")
    assert(shield.custom ~= nil and shield.custom.states ~= nil, "shield states table missing")
    assert(table.getn(shield.custom.states) == 2, "shield must have 2 states")
    local shieldKeys = {}
    for _, s in ipairs(shield.custom.states) do shieldKeys[s.key] = true end
    assert(shieldKeys[true] and shieldKeys[false], "shield state keys mismatch")
    assert(type(shield.set) == 'function', "shield set must be a function")

    -- 4. strategic_icon_outline_palette
    local palette = byKey['strategic_icon_outline_palette']
    assert(palette ~= nil, "strategic_icon_outline_palette item not found")
    assert(palette.type == 'toggle', "palette type must be toggle")
    assert(palette.default == 0, "palette default must be 0")
    assert(type(palette.title) == 'string' and string.len(palette.title) > 0, "palette title missing")
    assert(type(palette.tip) == 'string' and string.len(palette.tip) > 0, "palette tip missing")
    assert(palette.custom ~= nil and palette.custom.states ~= nil, "palette states table missing")
    assert(table.getn(palette.custom.states) == 4, "palette must have 4 states")
    local paletteKeys = {}
    for _, s in ipairs(palette.custom.states) do paletteKeys[s.key] = true end
    assert(paletteKeys[0] and paletteKeys[1] and paletteKeys[2] and paletteKeys[3], "palette state keys mismatch")
    assert(type(palette.set) == 'function', "palette set must be a function")
  )lua";

  lua_State* L = state.GetCState();
  const int status = lua_dostring(L, script);
  EXPECT_EQ(status, 0) << (lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error");
}

TEST_F(StrategicIconHpOutlineOptionsLuaTest, SetCallbacksInvokeNativeConfig)
{
  LuaPlus::LuaState state{LuaPlus::LuaState::LIB_BASE};
  SetupEnvironment(state, true);

  const char* script = R"lua(
    local items = options.ui.items
    local byKey = {}
    for i, item in ipairs(items) do
      if item.key and item.key ~= '' then
        byKey[item.key] = item
      end
    end

    -- Trigger set on mode
    byKey['strategic_icon_outline_mode'].set('strategic_icon_outline_mode', 2, false)

    -- Trigger set on cutoff
    byKey['strategic_icon_outline_smart_cutoff'].set('strategic_icon_outline_smart_cutoff', 0.80, false)

    -- Trigger set on shield
    byKey['strategic_icon_outline_shield'].set('strategic_icon_outline_shield', false, false)

    -- Trigger set on palette
    byKey['strategic_icon_outline_palette'].set('strategic_icon_outline_palette', 2, false)
  )lua";

  lua_State* L = state.GetCState();
  const int status = lua_dostring(L, script);
  ASSERT_EQ(status, 0) << (lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error");

  const auto config = moho::GetStrategicIconOutlineConfig();
  EXPECT_EQ(config.mMode, moho::EStrategicIconOutlineMode::Combined);
  EXPECT_NEAR(config.mSmartCutoff, 0.80f, 0.001f);
  EXPECT_FALSE(config.mShowShield);
  EXPECT_EQ(moho::ui_StrategicIconHpOutlinePalette, 2);
}

TEST_F(StrategicIconHpOutlineOptionsLuaTest, SetCallbacksFallbackToConExecuteWhenNativeBindingAbsent)
{
  LuaPlus::LuaState state{LuaPlus::LuaState::LIB_BASE};
  // Setup environment WITHOUT registering native bindings
  SetupEnvironment(state, false);

  const char* script = R"lua(
    executedCommands = {}
    ConExecute = function(cmd)
      table.insert(executedCommands, cmd)
    end

    local items = options.ui.items
    local byKey = {}
    for i, item in ipairs(items) do
      if item.key and item.key ~= '' then
        byKey[item.key] = item
      end
    end

    byKey['strategic_icon_outline_mode'].set('strategic_icon_outline_mode', 1, false)
    byKey['strategic_icon_outline_smart_cutoff'].set('strategic_icon_outline_smart_cutoff', 0.95, false)
    byKey['strategic_icon_outline_shield'].set('strategic_icon_outline_shield', false, false)
    byKey['strategic_icon_outline_palette'].set('strategic_icon_outline_palette', 3, false)

    assert(table.getn(executedCommands) == 4, "expected 4 commands, got " .. table.getn(executedCommands))
    assert(string.find(executedCommands[1], "ui_StrategicIconHpOutlineMode 1") ~= nil, "mode command mismatch: " .. executedCommands[1])
    local _, _, cutoffValStr = string.find(executedCommands[2], "ui_StrategicIconHpOutlineSmartCutoff%s+(%S+)")
    assert(cutoffValStr ~= nil, "could not parse cutoff command: " .. executedCommands[2])
    assert(math.abs(tonumber(cutoffValStr) - 0.95) < 0.001, "cutoff value mismatch: " .. cutoffValStr)
    assert(string.find(executedCommands[3], "ui_StrategicIconHpOutlineShield 0") ~= nil, "shield command mismatch: " .. executedCommands[3])
    assert(string.find(executedCommands[4], "ui_StrategicIconHpOutlinePalette 3") ~= nil, "palette command mismatch: " .. executedCommands[4])
  )lua";

  lua_State* L = state.GetCState();
  const int status = lua_dostring(L, script);
  EXPECT_EQ(status, 0) << (lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error");
}
