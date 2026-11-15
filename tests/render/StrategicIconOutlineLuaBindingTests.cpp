#include <gtest/gtest.h>
#include <cmath>

#include "lua/LuaObject.h"
#include "lua/LuaPrimitives.h"
#include "moho/sim/CWldSession.h"
#include "moho/misc/StartupHelpers.h"
#include "moho/lua/CScrLuaInitForm.h"

namespace
{
  class StrategicIconOutlineLuaBindingTest : public ::testing::Test
  {
  protected:
    LuaPlus::LuaState mState{LuaPlus::LuaState::LIB_BASE};

    void SetUp() override
    {
      // Reset config to defaults
      moho::StrategicIconOutlineConfig def;
      moho::SetStrategicIconOutlineConfig(def);
      moho::ui_StrategicIconHpOutlinePalette = 0;

      // Register Lua bindings into test state
      (void)moho::func_SetStrategicIconOutlineConfig_LuaFuncDef();
      (void)moho::func_GetStrategicIconOutlineConfig_LuaFuncDef();
      moho::SCR_UserLuaInitSet().RunInits(&mState);
    }

    void TearDown() override
    {
      moho::StrategicIconOutlineConfig def;
      moho::SetStrategicIconOutlineConfig(def);
      moho::ui_StrategicIconHpOutlinePalette = 0;
    }

    void ExecuteLua(const char* script)
    {
      lua_State* L = mState.GetCState();
      const int status = lua_dostring(L, script);
      if (status != 0) {
        const char* err = lua_tostring(L, -1);
        FAIL() << "Lua error: " << (err ? err : "(unknown)");
      }
    }
  };
}

TEST_F(StrategicIconOutlineLuaBindingTest, LuaTableRoundtrip)
{
  const char* script = R"(
    SetStrategicIconOutlineConfig({
      mode = 2,
      smart_cutoff = 0.90,
      shield = true,
      good_cutoff = 0.70,
      bad_cutoff = 0.25,
      color_good = 0xFF00FF66,
      color_med = 0xFFFFD700,
      color_bad = 0xFFFF0033,
      color_shield = 0xFF00E5FF
    })
    local cfg = GetStrategicIconOutlineConfig()
    assert(cfg ~= nil, "cfg is nil")
    assert(cfg.mode == 2, "mode mismatch: " .. tostring(cfg.mode))
    assert(math.abs(cfg.smart_cutoff - 0.90) < 0.001, "smart_cutoff mismatch")
    assert(cfg.shield == true, "shield mismatch")
    assert(math.abs(cfg.good_cutoff - 0.70) < 0.001, "good_cutoff mismatch")
    assert(math.abs(cfg.bad_cutoff - 0.25) < 0.001, "bad_cutoff mismatch")
    assert(cfg.color_good == 0xFF00FF66, "color_good mismatch")
    assert(cfg.color_med == 0xFFFFD700, "color_med mismatch")
    assert(cfg.color_bad == 0xFFFF0033, "color_bad mismatch")
    assert(cfg.color_shield == 0xFF00E5FF, "color_shield mismatch")
  )";
  ExecuteLua(script);

  const auto synced = moho::GetStrategicIconOutlineConfig();
  EXPECT_EQ(synced.mMode, moho::EStrategicIconOutlineMode::Sweep);
  EXPECT_NEAR(synced.mSmartCutoff, 0.90f, 0.001f);
  EXPECT_TRUE(synced.mShowShield);
  EXPECT_NEAR(synced.mGoodCutoff, 0.70f, 0.001f);
  EXPECT_NEAR(synced.mBadCutoff, 0.25f, 0.001f);
  EXPECT_EQ(synced.mColorGood, 0xFF00FF66u);
  EXPECT_EQ(synced.mColorMed, 0xFFFFD700u);
  EXPECT_EQ(synced.mColorBad, 0xFFFF0033u);
  EXPECT_EQ(synced.mColorShield, 0xFF00E5FFu);
}

TEST_F(StrategicIconOutlineLuaBindingTest, PalettePresets)
{
  // Test numeric palette 1 (Neon)
  const char* script1 = R"(
    SetStrategicIconOutlineConfig({
      palette = 1
    })
    local cfg = GetStrategicIconOutlineConfig()
    assert(cfg.palette == 1, "palette mismatch")
  )";
  ExecuteLua(script1);
  EXPECT_EQ(moho::ui_StrategicIconHpOutlinePalette, 1);
  auto config = moho::GetStrategicIconOutlineConfig();
  EXPECT_EQ(config.mColorGood, 0xFF00FF66u);
  EXPECT_EQ(config.mColorBad, 0xFFFF0033u);

  // Test string palette "colorblind" (Deuteranopia = 2)
  const char* script2 = R"(
    SetStrategicIconOutlineConfig({
      palette = "colorblind"
    })
    local cfg = GetStrategicIconOutlineConfig()
    assert(cfg.palette == 2, "palette mismatch")
  )";
  ExecuteLua(script2);
  EXPECT_EQ(moho::ui_StrategicIconHpOutlinePalette, 2);
  config = moho::GetStrategicIconOutlineConfig();
  EXPECT_EQ(config.mColorGood, 0xFF3B82F6u);
  EXPECT_EQ(config.mColorBad, 0xFFF43F5Eu);
}

TEST_F(StrategicIconOutlineLuaBindingTest, HexStringColors)
{
  const char* script = R"(
    SetStrategicIconOutlineConfig({
      color_good = "#22C55E",
      color_bad = "0xEF4444",
      color_shield = "00C8FF"
    })
    local cfg = GetStrategicIconOutlineConfig()
    assert(cfg ~= nil)
  )";
  ExecuteLua(script);

  const auto config = moho::GetStrategicIconOutlineConfig();
  EXPECT_EQ(config.mColorGood, 0xFF22C55Eu);
  EXPECT_EQ(config.mColorBad, 0xFFEF4444u);
  EXPECT_EQ(config.mColorShield, 0xFF00C8FFu);
}

TEST_F(StrategicIconOutlineLuaBindingTest, GracefulHandlingOfNonTableOrInvalidMode)
{
  // Call with non-table should not crash or alter existing config
  const char* script1 = R"(
    SetStrategicIconOutlineConfig("invalid")
    SetStrategicIconOutlineConfig(12345)
    SetStrategicIconOutlineConfig(nil)
  )";
  ExecuteLua(script1);

  // Call with invalid mode should fallback to None
  const char* script2 = R"(
    SetStrategicIconOutlineConfig({
      mode = 99
    })
    local cfg = GetStrategicIconOutlineConfig()
    assert(cfg.mode == 0, "mode should fallback to 0")
  )";
  ExecuteLua(script2);
  EXPECT_EQ(moho::GetStrategicIconOutlineConfig().mMode, moho::EStrategicIconOutlineMode::None);
}

TEST_F(StrategicIconOutlineLuaBindingTest, CategoryContentPlacementPulseRoundtrip)
{
  // 1. Numeric arguments
  const char* scriptNum = R"(
    SetStrategicIconOutlineConfig({
      category = 2,
      content = 1,
      placement = 3,
      pulse = false
    })
    local cfg = GetStrategicIconOutlineConfig()
    assert(cfg.category == 2, "category mismatch: " .. tostring(cfg.category))
    assert(cfg.content == 1, "content mismatch: " .. tostring(cfg.content))
    assert(cfg.placement == 3, "placement mismatch: " .. tostring(cfg.placement))
    assert(cfg.pulse == false, "pulse mismatch")
  )";
  ExecuteLua(scriptNum);

  auto config = moho::GetStrategicIconOutlineConfig();
  EXPECT_EQ(config.mCategoryFilter, 2);
  EXPECT_EQ(config.mContent, 1);
  EXPECT_EQ(config.mPlacement, 3);
  EXPECT_FALSE(config.mPulse);

  EXPECT_EQ(moho::ui_StrategicIconHpOutlineCategory, 2);
  EXPECT_EQ(moho::ui_StrategicIconHpOutlineContent, 1);
  EXPECT_EQ(moho::ui_StrategicIconHpOutlinePlacement, 3);
  EXPECT_EQ(moho::ui_StrategicIconHpOutlinePulse, 0);

  // 2. String arguments
  const char* scriptStr = R"(
    SetStrategicIconOutlineConfig({
      category = "commander",
      content = "shield",
      placement = "top",
      pulse = true
    })
    local cfg = GetStrategicIconOutlineConfig()
    assert(cfg.category == 1, "category mismatch: " .. tostring(cfg.category))
    assert(cfg.content == 2, "content mismatch: " .. tostring(cfg.content))
    assert(cfg.placement == 1, "placement mismatch: " .. tostring(cfg.placement))
    assert(cfg.pulse == true, "pulse mismatch")
  )";
  ExecuteLua(scriptStr);

  config = moho::GetStrategicIconOutlineConfig();
  EXPECT_EQ(config.mCategoryFilter, 1);
  EXPECT_EQ(config.mContent, 2);
  EXPECT_EQ(config.mPlacement, 1);
  EXPECT_TRUE(config.mPulse);

  // 3. String aliases
  const char* scriptAliases = R"(
    SetStrategicIconOutlineConfig({
      category = "experimental",
      content = "both",
      placement = "split_tb"
    })
    local cfg = GetStrategicIconOutlineConfig()
    assert(cfg.category == 2, "category mismatch")
    assert(cfg.content == 0, "content mismatch")
    assert(cfg.placement == 3, "placement mismatch")
  )";
  ExecuteLua(scriptAliases);

  config = moho::GetStrategicIconOutlineConfig();
  EXPECT_EQ(config.mCategoryFilter, 2);
  EXPECT_EQ(config.mContent, 0);
  EXPECT_EQ(config.mPlacement, 3);

  // 4. Verify content = 0 does not mutate to 1 when shield = false
  const char* scriptContentShield = R"(
    SetStrategicIconOutlineConfig({
      content = 0,
      shield = false
    })
    local cfg = GetStrategicIconOutlineConfig()
    assert(cfg.content == 0, "content corrupted on shield=false: " .. tostring(cfg.content))
    assert(cfg.shield == false, "shield mismatch")
  )";
  ExecuteLua(scriptContentShield);
  config = moho::GetStrategicIconOutlineConfig();
  EXPECT_EQ(config.mContent, 0);
  EXPECT_FALSE(config.mShowShield);

  // 5. Test invalid table category does not crash (caught gracefully)
  const char* scriptInvalidCat = R"(
    SetStrategicIconOutlineConfig({
      category = { foo = "bar" }
    })
    local cfg = GetStrategicIconOutlineConfig()
    assert(cfg ~= nil)
  )";
  ExecuteLua(scriptInvalidCat);
}
