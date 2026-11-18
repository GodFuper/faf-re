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

  std::filesystem::path FindFafengineOptionsLua()
  {
    std::error_code ec;
    const std::filesystem::path wellKnown = "I:/ProjectsFaf/faf-re/data/fafengine/lua/options/options.lua";
    if (std::filesystem::exists(wellKnown, ec)) {
      return wellKnown;
    }

    const std::filesystem::path cur = std::filesystem::current_path(ec);
    for (const auto& rel : {
           "data/fafengine/lua/options/options.lua",
           "../data/fafengine/lua/options/options.lua",
           "../../data/fafengine/lua/options/options.lua",
           "../../../data/fafengine/lua/options/options.lua"
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
    ConTextMatches = function(c) return {c} end
  )lua";

  class StrategicIconHpOutlineOptionsLuaTest : public ::testing::Test
  {
  protected:
    std::string mOptionsLuaSource;
    std::filesystem::path mOptionsLuaPath;
    std::string mEngineHookLuaSource;
    std::filesystem::path mEngineHookLuaPath;

    void SetUp() override
    {
      mOptionsLuaPath = FindOptionsLua();
      ASSERT_FALSE(mOptionsLuaPath.empty()) << "Could not locate fa/lua/options/options.lua";
      mOptionsLuaSource = ReadFileToString(mOptionsLuaPath);
      ASSERT_FALSE(mOptionsLuaSource.empty()) << "options.lua was empty or could not be read: " << mOptionsLuaPath;

      mEngineHookLuaPath = FindFafengineOptionsLua();
      if (!mEngineHookLuaPath.empty()) {
        mEngineHookLuaSource = ReadFileToString(mEngineHookLuaPath);
      }

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

      if (!mEngineHookLuaSource.empty()) {
        status = lua_dostring(L, mEngineHookLuaSource.c_str());
        ASSERT_EQ(status, 0) << "Failed to load/execute fafengine options.lua: " << lua_tostring(L, -1);
      }
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
  if (status != 0) {
    const char* err = lua_tostring(L, -1);
    std::cout << "LUA SCRIPT ERROR: " << (err ? err : "null") << " (top=" << lua_gettop(L) << ")" << std::endl;
  }
  EXPECT_EQ(status, 0) << (lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error");
}

TEST_F(StrategicIconHpOutlineOptionsLuaTest, AllSixOutlineOptionsPresentAndValid)
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

    -- Subcategory header
    local header = byKey['strategic_icons_header']
    assert(header ~= nil, "strategic_icons_header item not found")
    assert(header.type == 'header', "header type must be header")
    assert(type(header.title) == 'string' and string.len(header.title) > 0, "header title missing")

    -- 1. strategic_icon_outline_mode
    local mode = byKey['strategic_icon_outline_mode']
    assert(mode ~= nil, "strategic_icon_outline_mode item not found")
    assert(mode.type == 'toggle', "mode type must be toggle")
    assert(mode.default == 0, "mode default must be 0")
    assert(type(mode.title) == 'string' and string.len(mode.title) > 0, "mode title missing")
    assert(type(mode.tip) == 'string' and string.len(mode.tip) > 0, "mode tip missing")
    assert(mode.custom ~= nil and mode.custom.states ~= nil, "mode states table missing")
    assert(table.getn(mode.custom.states) == 3, "mode must have 3 states")
    local modeKeys = {}
    for _, s in ipairs(mode.custom.states) do modeKeys[s.key] = true end
    assert(modeKeys[0] and modeKeys[1] and modeKeys[2], "mode state keys mismatch")
    assert(type(mode.set) == 'function', "mode set must be a function")

    -- 2. strategic_icon_outline_content
    local content = byKey['strategic_icon_outline_content']
    assert(content ~= nil, "strategic_icon_outline_content item not found")
    assert(content.type == 'toggle', "content type must be toggle")
    assert(content.default == 0, "content default must be 0")
    assert(type(content.title) == 'string' and string.len(content.title) > 0, "content title missing")
    assert(type(content.tip) == 'string' and string.len(content.tip) > 0, "content tip missing")
    assert(content.custom ~= nil and content.custom.states ~= nil, "content states table missing")
    assert(table.getn(content.custom.states) == 3, "content must have 3 states")
    local contentKeys = {}
    for _, s in ipairs(content.custom.states) do contentKeys[s.key] = true end
    assert(contentKeys[0] and contentKeys[1] and contentKeys[2], "content state keys mismatch")
    assert(type(content.set) == 'function', "content set must be a function")

    -- 3. strategic_icon_outline_placement
    local placement = byKey['strategic_icon_outline_placement']
    assert(placement ~= nil, "strategic_icon_outline_placement item not found")
    assert(placement.type == 'toggle', "placement type must be toggle")
    assert(placement.default == 0, "placement default must be 0")
    assert(type(placement.title) == 'string' and string.len(placement.title) > 0, "placement title missing")
    assert(type(placement.tip) == 'string' and string.len(placement.tip) > 0, "placement tip missing")
    assert(placement.custom ~= nil and placement.custom.states ~= nil, "placement states table missing")
    assert(table.getn(placement.custom.states) == 4, "placement must have 4 states")
    local placementKeys = {}
    for _, s in ipairs(placement.custom.states) do placementKeys[s.key] = true end
    assert(placementKeys[0] and placementKeys[1] and placementKeys[2] and placementKeys[3], "placement state keys mismatch")
    assert(type(placement.set) == 'function', "placement set must be a function")

    -- 4. strategic_icon_outline_category
    local category = byKey['strategic_icon_outline_category']
    assert(category ~= nil, "strategic_icon_outline_category item not found")
    assert(category.type == 'toggle', "category type must be toggle")
    assert(category.default == 1, "category default must be 1 (Commander)")
    assert(type(category.title) == 'string' and string.len(category.title) > 0, "category title missing")
    assert(type(category.tip) == 'string' and string.len(category.tip) > 0, "category tip missing")
    assert(category.custom ~= nil and category.custom.states ~= nil, "category states table missing")
    assert(table.getn(category.custom.states) == 4, "category must have 4 states")
    local categoryKeys = {}
    for _, s in ipairs(category.custom.states) do categoryKeys[s.key] = true end
    assert(categoryKeys[0] and categoryKeys[1] and categoryKeys[2] and categoryKeys[-1], "category state keys mismatch")
    assert(type(category.set) == 'function', "category set must be a function")

    -- 5. strategic_icon_outline_smart_cutoff
    local cutoff = byKey['strategic_icon_outline_smart_cutoff']
    assert(cutoff ~= nil, "strategic_icon_outline_smart_cutoff item not found")
    assert(cutoff.type == 'toggle', "cutoff type must be toggle")
    assert(math.abs(cutoff.default - 0.0) < 0.001, "cutoff default must be 0.0")
    assert(type(cutoff.title) == 'string' and string.len(cutoff.title) > 0, "cutoff title missing")
    assert(type(cutoff.tip) == 'string' and string.len(cutoff.tip) > 0, "cutoff tip missing")
    assert(cutoff.custom ~= nil and cutoff.custom.states ~= nil, "cutoff states table missing")
    assert(table.getn(cutoff.custom.states) == 9, "cutoff must have 9 states")
    local cutoffKeys = {}
    for _, s in ipairs(cutoff.custom.states) do cutoffKeys[s.key] = true end
    assert(cutoffKeys[0.0] and cutoffKeys[1.0] and cutoffKeys[0.95] and cutoffKeys[0.90] and cutoffKeys[0.80] and cutoffKeys[0.75] and cutoffKeys[0.50] and cutoffKeys[0.40] and cutoffKeys[0.25], "cutoff state keys mismatch")
    assert(type(cutoff.set) == 'function', "cutoff set must be a function")

    -- 6. strategic_icon_outline_palette
    local palette = byKey['strategic_icon_outline_palette']
    assert(palette ~= nil, "strategic_icon_outline_palette item not found")
    assert(palette.type == 'toggle', "palette type must be toggle")
    assert(palette.default == 0, "palette default must be 0")
    assert(type(palette.title) == 'string' and string.len(palette.title) > 0, "palette title missing")
    assert(type(palette.tip) == 'string' and string.len(palette.tip) > 0, "palette tip missing")
    assert(palette.custom ~= nil and palette.custom.states ~= nil, "palette states table missing")
    assert(table.getn(palette.custom.states) == 5, "palette must have 5 states")
    local paletteKeys = {}
    for _, s in ipairs(palette.custom.states) do paletteKeys[s.key] = true end
    assert(paletteKeys[0] and paletteKeys[1] and paletteKeys[2] and paletteKeys[3] and paletteKeys[4], "palette state keys mismatch")
    assert(type(palette.set) == 'function', "palette set must be a function")
  )lua";

  lua_State* L = state.GetCState();
  const int status = lua_dostring(L, script);
  if (status != 0) {
    const char* err = lua_tostring(L, -1);
    std::cout << "LUA SCRIPT ERROR: " << (err ? err : "null") << " (top=" << lua_gettop(L) << ")" << std::endl;
  }
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

    -- Trigger set on content
    byKey['strategic_icon_outline_content'].set('strategic_icon_outline_content', 1, false)

    -- Trigger set on placement
    byKey['strategic_icon_outline_placement'].set('strategic_icon_outline_placement', 3, false)

    -- Trigger set on category
    byKey['strategic_icon_outline_category'].set('strategic_icon_outline_category', 2, false)

    -- Trigger set on cutoff
    byKey['strategic_icon_outline_smart_cutoff'].set('strategic_icon_outline_smart_cutoff', 0.80, false)

    -- Trigger set on palette
    byKey['strategic_icon_outline_palette'].set('strategic_icon_outline_palette', 2, false)
  )lua";

  lua_State* L = state.GetCState();
  const int status = lua_dostring(L, script);
  ASSERT_EQ(status, 0) << (lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error");

  const auto config = moho::GetStrategicIconOutlineConfig();
  EXPECT_EQ(config.mMode, moho::EStrategicIconOutlineMode::Sweep);
  EXPECT_EQ(config.mContent, 1);
  EXPECT_EQ(config.mPlacement, 3);
  EXPECT_EQ(config.mCategoryFilter, 2);
  EXPECT_NEAR(config.mSmartCutoff, 0.80f, 0.001f);
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
    byKey['strategic_icon_outline_content'].set('strategic_icon_outline_content', 2, false)
    byKey['strategic_icon_outline_placement'].set('strategic_icon_outline_placement', 3, false)
    byKey['strategic_icon_outline_category'].set('strategic_icon_outline_category', 2, false)
    byKey['strategic_icon_outline_smart_cutoff'].set('strategic_icon_outline_smart_cutoff', 0.95, false)
    byKey['strategic_icon_outline_palette'].set('strategic_icon_outline_palette', 3, false)

    assert(table.getn(executedCommands) == 6, "expected 6 commands, got " .. table.getn(executedCommands))
    assert(string.find(executedCommands[1], "ui_StrategicIconHpOutlineMode 1") ~= nil, "mode command mismatch: " .. executedCommands[1])
    assert(string.find(executedCommands[2], "ui_StrategicIconHpOutlineContent 2") ~= nil, "content command mismatch: " .. executedCommands[2])
    assert(string.find(executedCommands[3], "ui_StrategicIconHpOutlinePlacement 3") ~= nil, "placement command mismatch: " .. executedCommands[3])
    assert(string.find(executedCommands[4], "ui_StrategicIconHpOutlineCategory 2") ~= nil, "category command mismatch: " .. executedCommands[4])
    local _, _, cutoffValStr = string.find(executedCommands[5], "ui_StrategicIconHpOutlineSmartCutoff%s+(%S+)")
    assert(cutoffValStr ~= nil, "could not parse cutoff command: " .. executedCommands[5])
    assert(math.abs(tonumber(cutoffValStr) - 0.95) < 0.001, "cutoff value mismatch: " .. cutoffValStr)
    assert(string.find(executedCommands[6], "ui_StrategicIconHpOutlinePalette 3") ~= nil, "palette command mismatch: " .. executedCommands[6])
  )lua";

  lua_State* L = state.GetCState();
  const int status = lua_dostring(L, script);
  if (status != 0) {
    const char* err = lua_tostring(L, -1);
    std::cout << "LUA SCRIPT ERROR: " << (err ? err : "null") << " (top=" << lua_gettop(L) << ")" << std::endl;
  }
  EXPECT_EQ(status, 0) << (lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error");
}
