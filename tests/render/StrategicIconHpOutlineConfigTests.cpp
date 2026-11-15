#include <gtest/gtest.h>
#include "moho/sim/CWldSession.h"

namespace moho
{
  extern int ui_StrategicIconHpOutlineMode;
  extern float ui_StrategicIconHpOutlineSmartCutoff;
  extern int ui_StrategicIconHpOutlineShield;
  extern int ui_StrategicIconHpOutlinePalette;
}

TEST(StrategicIconHpOutlineConfigTest, DefaultsAndModeTransitions)
{
  using namespace moho;

  // Verify initial default ConVar values
  EXPECT_EQ(ui_StrategicIconHpOutlineMode, 0);
  EXPECT_FLOAT_EQ(ui_StrategicIconHpOutlineSmartCutoff, 1.0f);
  EXPECT_EQ(ui_StrategicIconHpOutlineShield, 1);
  EXPECT_EQ(ui_StrategicIconHpOutlinePalette, 0);

  // Verify default struct values
  StrategicIconOutlineConfig defaultConfig;
  EXPECT_EQ(defaultConfig.mMode, EStrategicIconOutlineMode::None);
  EXPECT_FLOAT_EQ(defaultConfig.mSmartCutoff, 1.0f);
  EXPECT_TRUE(defaultConfig.mShowShield);
  EXPECT_FLOAT_EQ(defaultConfig.mGoodCutoff, 0.65f);
  EXPECT_FLOAT_EQ(defaultConfig.mBadCutoff, 0.30f);
  EXPECT_EQ(defaultConfig.mColorGood, 0xFF22C55Eu);
  EXPECT_EQ(defaultConfig.mColorMed, 0xFFEAB308u);
  EXPECT_EQ(defaultConfig.mColorBad, 0xFFEF4444u);
  EXPECT_EQ(defaultConfig.mColorShield, 0xFF00C8FFu);

  // Mutate config and apply
  StrategicIconOutlineConfig config;
  config.mMode = EStrategicIconOutlineMode::Smart;
  config.mSmartCutoff = 0.95f;
  config.mShowShield = false;
  config.mGoodCutoff = 0.70f;
  config.mBadCutoff = 0.25f;
  config.mColorGood = 0xFF00FF00u;
  config.mColorMed = 0xFFFFFF00u;
  config.mColorBad = 0xFFFF0000u;
  config.mColorShield = 0xFF00FFFFu;

  SetStrategicIconOutlineConfig(config);
  const auto retrieved = GetStrategicIconOutlineConfig();

  EXPECT_EQ(retrieved.mMode, EStrategicIconOutlineMode::Smart);
  EXPECT_FLOAT_EQ(retrieved.mSmartCutoff, 0.95f);
  EXPECT_FALSE(retrieved.mShowShield);
  EXPECT_FLOAT_EQ(retrieved.mGoodCutoff, 0.70f);
  EXPECT_FLOAT_EQ(retrieved.mBadCutoff, 0.25f);
  EXPECT_EQ(retrieved.mColorGood, 0xFF00FF00u);
  EXPECT_EQ(retrieved.mColorMed, 0xFFFFFF00u);
  EXPECT_EQ(retrieved.mColorBad, 0xFFFF0000u);
  EXPECT_EQ(retrieved.mColorShield, 0xFF00FFFFu);

  // Verify ConVar synchronization
  EXPECT_EQ(ui_StrategicIconHpOutlineMode, 3);
  EXPECT_FLOAT_EQ(ui_StrategicIconHpOutlineSmartCutoff, 0.95f);
  EXPECT_EQ(ui_StrategicIconHpOutlineShield, 0);

  // Test mutating ConVar directly reflects in GetStrategicIconOutlineConfig
  ui_StrategicIconHpOutlineMode = 2; // Combined
  ui_StrategicIconHpOutlineSmartCutoff = 0.80f;
  ui_StrategicIconHpOutlineShield = 1;

  const auto synced = GetStrategicIconOutlineConfig();
  EXPECT_EQ(synced.mMode, EStrategicIconOutlineMode::Combined);
  EXPECT_FLOAT_EQ(synced.mSmartCutoff, 0.80f);
  EXPECT_TRUE(synced.mShowShield);

  // Test invalid out-of-bounds ConVar mode falls back safely to None
  ui_StrategicIconHpOutlineMode = 99;
  EXPECT_EQ(GetStrategicIconOutlineConfig().mMode, EStrategicIconOutlineMode::None);

  // Reset back to defaults for clean state
  SetStrategicIconOutlineConfig(defaultConfig);
  EXPECT_EQ(ui_StrategicIconHpOutlineMode, 0);
  EXPECT_FLOAT_EQ(ui_StrategicIconHpOutlineSmartCutoff, 1.0f);
  EXPECT_EQ(ui_StrategicIconHpOutlineShield, 1);
}
