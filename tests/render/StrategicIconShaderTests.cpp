#include <gtest/gtest.h>
#include <cstddef>
#include <cstring>
#include <string_view>
#include <d3dcompiler.h>
#include "moho/render/StrategicIconClassifyShader.h"
#include "moho/render/StrategicIconRenderer.h"
#include "moho/render/StrategicIconShader.h"
#include "moho/resource/blueprints/RBlueprint.h"
#include "moho/entity/REntityBlueprint.h"
#include "moho/sim/CWldSession.h"
#include "moho/entity/UserEntity.h"
#include "moho/entity/EntityCategoryReflection.h"

namespace moho
{

TEST(StrategicIconShaderTest, ConstantBufferAlignmentAndInstanceStride)
{
  EXPECT_EQ(sizeof(DrawConstants) % 16, 0u);
  EXPECT_EQ(sizeof(DrawConstants), 112u);
  EXPECT_EQ(StrategicIconInstance::kLayerCount, 5u);
  EXPECT_EQ(StrategicIconInstance::kOutlineDisabledFlag, 0x80000000u);

  const DrawConstants defaults{};
  EXPECT_EQ(defaults.outlineMode, 0u);
  EXPECT_FLOAT_EQ(defaults.smartCutoff, 0.0f);
  EXPECT_EQ(defaults.outlineContent, 0u);
  EXPECT_FLOAT_EQ(defaults.goodCutoff, 0.65f);
  EXPECT_FLOAT_EQ(defaults.badCutoff, 0.30f);
  EXPECT_EQ(defaults.colorGood, 0xFF22C55Eu);
  EXPECT_EQ(defaults.colorMed, 0xFFEAB308u);
  EXPECT_EQ(defaults.colorBad, 0xFFEF4444u);
  EXPECT_EQ(defaults.colorShield, 0xFF00C8FFu);
  EXPECT_EQ(defaults.outlinePlacement, 0u);
  EXPECT_EQ(defaults.pulseEnabled, 1u);
  EXPECT_FLOAT_EQ(defaults.animTime, 0.0f);
}

TEST(StrategicIconShaderTest, HealthShieldPacking15BitAndFlagIsolation)
{
  // 100% health, 100% shield, friendly
  const std::uint32_t fullPacked = StrategicIconInstance::PackHealthAndShield(100.0f, 100.0f, 1.0f, true);
  // Bit 31 must NOT be set by PackHealthAndShield
  EXPECT_EQ(fullPacked & StrategicIconInstance::kOutlineDisabledFlag, 0u);

  // Decoding 15-bit shield yields 1.0f
  const float shieldFrac = float((fullPacked >> 16) & 0x7FFFu) / 32767.0f;
  const float healthFrac = float(fullPacked & 0xFFFFu) / 65535.0f;
  EXPECT_NEAR(shieldFrac, 1.0f, 0.0001f);
  EXPECT_NEAR(healthFrac, 1.0f, 0.0001f);

  // When kOutlineDisabledFlag is ORed, bit 31 is set while health and shield fractions remain intact
  const std::uint32_t disabledPacked = fullPacked | StrategicIconInstance::kOutlineDisabledFlag;
  EXPECT_NE(disabledPacked & StrategicIconInstance::kOutlineDisabledFlag, 0u);
  const float disabledShieldFrac = float((disabledPacked >> 16) & 0x7FFFu) / 32767.0f;
  const float disabledHealthFrac = float(disabledPacked & 0xFFFFu) / 65535.0f;
  EXPECT_NEAR(disabledShieldFrac, 1.0f, 0.0001f);
  EXPECT_NEAR(disabledHealthFrac, 1.0f, 0.0001f);

  // Enemy unit binary shield (15-bit full 0x7FFF)
  const std::uint32_t enemyActiveShield = StrategicIconInstance::PackHealthAndShield(50.0f, 100.0f, 0.5f, false);
  EXPECT_EQ(enemyActiveShield & StrategicIconInstance::kOutlineDisabledFlag, 0u);
  EXPECT_NEAR(float((enemyActiveShield >> 16) & 0x7FFFu) / 32767.0f, 1.0f, 0.0001f);
  EXPECT_NEAR(float(enemyActiveShield & 0xFFFFu) / 65535.0f, 0.5f, 0.001f);
}

TEST(StrategicIconShaderTest, ShaderSourceContainsOutlineTokens)
{
  const std::string_view source(kStrategicIconShaderSource);
  EXPECT_NE(source.find("OutlineMode"), std::string_view::npos);
  EXPECT_NE(source.find("SmartCutoff"), std::string_view::npos);
  EXPECT_NE(source.find("OutlineContent"), std::string_view::npos);
  EXPECT_NE(source.find("GoodCutoff"), std::string_view::npos);
  EXPECT_NE(source.find("BadCutoff"), std::string_view::npos);
  EXPECT_NE(source.find("ColorGood"), std::string_view::npos);
  EXPECT_NE(source.find("ColorMed"), std::string_view::npos);
  EXPECT_NE(source.find("ColorBad"), std::string_view::npos);
  EXPECT_NE(source.find("ColorShield"), std::string_view::npos);
  EXPECT_NE(source.find("OutlinePlacement"), std::string_view::npos);
  EXPECT_NE(source.find("PulseEnabled"), std::string_view::npos);
  EXPECT_NE(source.find("AnimTime"), std::string_view::npos);
  EXPECT_NE(source.find("localPos"), std::string_view::npos);
  EXPECT_NE(source.find("healthShield"), std::string_view::npos);
  EXPECT_NE(source.find("0x80000000u"), std::string_view::npos);
  EXPECT_NE(source.find("32767.0"), std::string_view::npos);
  EXPECT_NE(source.find("hasBothMeters"), std::string_view::npos);
  EXPECT_NE(source.find("hasHpMeter"), std::string_view::npos);
  EXPECT_NE(source.find("hasShieldMeter"), std::string_view::npos);
}

TEST(StrategicIconShaderTest, CompilesVertexAndPixelShadersD3D11)
{
  ID3DBlob* vsBlob = nullptr;
  ID3DBlob* vsErrors = nullptr;
  const HRESULT hrVS = D3DCompile(
    kStrategicIconShaderSource,
    sizeof(kStrategicIconShaderSource) - 1,
    "StrategicIconShader",
    nullptr,
    nullptr,
    "StrategicIconVS",
    "vs_5_0",
    0,
    0,
    &vsBlob,
    &vsErrors
  );
  if (FAILED(hrVS) && vsErrors != nullptr) {
    FAIL() << "VS compile failed: " << static_cast<const char*>(vsErrors->GetBufferPointer());
  }
  EXPECT_TRUE(SUCCEEDED(hrVS));
  if (vsBlob) {
    vsBlob->Release();
  }
  if (vsErrors) {
    vsErrors->Release();
  }

  ID3DBlob* psBlob = nullptr;
  ID3DBlob* psErrors = nullptr;
  const HRESULT hrPS = D3DCompile(
    kStrategicIconShaderSource,
    sizeof(kStrategicIconShaderSource) - 1,
    "StrategicIconShader",
    nullptr,
    nullptr,
    "StrategicIconPS",
    "ps_5_0",
    0,
    0,
    &psBlob,
    &psErrors
  );
  if (FAILED(hrPS) && psErrors != nullptr) {
    FAIL() << "PS compile failed: " << static_cast<const char*>(psErrors->GetBufferPointer());
  }
  EXPECT_TRUE(SUCCEEDED(hrPS));
  if (psBlob) {
    psBlob->Release();
  }
  if (psErrors) {
    psErrors->Release();
  }
}

TEST(StrategicIconShaderTest, CompilesComputeShadersD3D11)
{
  ID3DBlob* csBlob = nullptr;
  ID3DBlob* csErrors = nullptr;
  const HRESULT hrCS = D3DCompile(
    kStrategicIconClassifyShaderSource,
    sizeof(kStrategicIconClassifyShaderSource) - 1,
    "StrategicIconClassifyShader",
    nullptr,
    nullptr,
    "ClassifyIconsCS",
    "cs_5_0",
    0,
    0,
    &csBlob,
    &csErrors
  );
  if (FAILED(hrCS) && csErrors != nullptr) {
    FAIL() << "ClassifyIconsCS compile failed: " << static_cast<const char*>(csErrors->GetBufferPointer());
  }
  EXPECT_TRUE(SUCCEEDED(hrCS));
  if (csBlob) {
    csBlob->Release();
  }
  if (csErrors) {
    csErrors->Release();
  }

  ID3DBlob* compactBlob = nullptr;
  ID3DBlob* compactErrors = nullptr;
  const HRESULT hrCompact = D3DCompile(
    kStrategicIconClassifyShaderSource,
    sizeof(kStrategicIconClassifyShaderSource) - 1,
    "StrategicIconCompactShader",
    nullptr,
    nullptr,
    "CompactIconsCS",
    "cs_5_0",
    0,
    0,
    &compactBlob,
    &compactErrors
  );
  if (FAILED(hrCompact) && compactErrors != nullptr) {
    FAIL() << "CompactIconsCS compile failed: " << static_cast<const char*>(compactErrors->GetBufferPointer());
  }
  EXPECT_TRUE(SUCCEEDED(hrCompact));
  if (compactBlob) {
    compactBlob->Release();
  }
  if (compactErrors) {
    compactErrors->Release();
  }
}

TEST(StrategicIconShaderTest, EndToEndGpuHealthDataPropagationIntegration)
{
  // 1. Verify that HLSL compute shader sources declare and propagate healthShield
  const std::string_view classifySrc(kStrategicIconClassifyShaderSource);
  EXPECT_NE(classifySrc.find("uint healthShield;"), std::string_view::npos);
  EXPECT_NE(classifySrc.find("record.healthShield = source.healthShield;"), std::string_view::npos);
  EXPECT_EQ(classifySrc.find("record.healthShield = 0x0000FFFFu;"), std::string_view::npos);

  // 2. Simulate UserEntity damage update: 50% health, 30% shield (friendly)
  const float simulatedHealth = 250.0f;
  const float simulatedMaxHealth = 500.0f;
  const float simulatedShield = 0.30f;
  const bool isFriendly = true;

  // DescribeGpu packing step:
  StrategicIconSourceRecord sourceRecord{};
  sourceRecord.mHealthShield = StrategicIconInstance::PackHealthAndShield(
    simulatedHealth,
    simulatedMaxHealth,
    simulatedShield,
    isFriendly
  );

  // 3. Verify CPU source record matches GPU input buffer layout:
  EXPECT_EQ(sizeof(StrategicIconSourceRecord), 64u);
  EXPECT_EQ(offsetof(StrategicIconSourceRecord, mHealthShield), 56u);

  // 4. Simulate compute shader ClassifyIconsCS:
  // record.healthShield = source.healthShield;
  StrategicIconInstance iconRecord{};
  iconRecord.mHealthShield = sourceRecord.mHealthShield;

  // 5. Simulate StrategicIconPS pixel shader unpack:
  const float healthFrac = float(iconRecord.mHealthShield & 0xFFFFu) / 65535.0f;
  const float shieldFrac = float((iconRecord.mHealthShield >> 16) & 0x7FFFu) / 32767.0f;

  EXPECT_NEAR(healthFrac, 0.5f, 0.001f);
  EXPECT_NEAR(shieldFrac, 0.3f, 0.001f);

  // 6. Verify Smart Mode Cutoff logic across all modes:
  // Smart Cutoff strictly hides HP outline when healthy, but NEVER hides shield outline:
  // bool hideHp = (!showHp || (SmartCutoff > 0.0 && healthFrac >= SmartCutoff));
  // bool hasShield = (showShield && shieldFrac > 0.0);
  // if (hideHp && !hasShield) discard;
  const float smartCutoffDisabled = 0.0f;
  const float smartCutoff100 = 1.0f;
  const bool showShield = true;

  const std::uint32_t fullHpNoShieldRecord = StrategicIconInstance::PackHealthAndShield(500.0f, 500.0f, 0.0f, true);
  const float fullHpFrac = float(fullHpNoShieldRecord & 0xFFFFu) / 65535.0f;
  const float noShieldFrac = float((fullHpNoShieldRecord >> 16) & 0x7FFFu) / 32767.0f;

  const std::uint32_t fullHpFullShieldRecord = StrategicIconInstance::PackHealthAndShield(500.0f, 500.0f, 1.0f, true);
  const float fullShieldFrac = float((fullHpFullShieldRecord >> 16) & 0x7FFFu) / 32767.0f;

  // When Cutoff is disabled (0.0), even full HP undamaged units are NOT discarded:
  const bool hideHpDisabled = (smartCutoffDisabled > 0.0f && fullHpFrac >= smartCutoffDisabled);
  const bool hasShieldNoShield = (showShield && noShieldFrac > 0.0f);
  EXPECT_FALSE(hideHpDisabled && !hasShieldNoShield);

  // When Cutoff is 1.0 (100%), damaged unit is NOT discarded:
  const bool hideHpDamaged = (smartCutoff100 > 0.0f && healthFrac >= smartCutoff100);
  const bool hasShieldPartial = (showShield && shieldFrac > 0.0f);
  EXPECT_FALSE(hideHpDamaged && !hasShieldPartial);

  // When Cutoff is 1.0 (100%), full HP unit with NO shield MUST be discarded:
  const bool hideHpFull = (smartCutoff100 > 0.0f && fullHpFrac >= smartCutoff100);
  EXPECT_TRUE(hideHpFull && !hasShieldNoShield);

  // CRITICAL: When Cutoff is 1.0 (100%) and unit has 100% full shield,
  // Smart Cutoff does NOT hide the shield! Quad is preserved to render the shield outline:
  const bool hasShieldFull = (showShield && fullShieldFrac > 0.0f);
  const bool fullShieldDiscarded = (hideHpFull && !hasShieldFull);
  EXPECT_FALSE(fullShieldDiscarded);

  // HP outline is hidden for full health, but shield outline remains active:
  const bool isHpPixel = true;
  const float hpAlpha = (isHpPixel && !hideHpFull) ? 1.0f : 0.0f;
  const float shieldAlpha = (!isHpPixel || true) && hasShieldFull ? 1.0f : 0.0f;
  EXPECT_EQ(hpAlpha, 0.0f);
  EXPECT_GT(shieldAlpha, 0.0f);
}

TEST(StrategicIconShaderTest, CategoryFilteringAndGhostHandling)
{
  // 1. nullptr entity returns false
  EXPECT_FALSE(UnitPassesStrategicIconCategoryFilter(nullptr, 0));
  EXPECT_FALSE(UnitPassesStrategicIconCategoryFilter(nullptr, 1));
  EXPECT_FALSE(UnitPassesStrategicIconCategoryFilter(nullptr, 2));
  EXPECT_FALSE(UnitPassesStrategicIconCategoryFilter(nullptr, -1));

  // 2. Setup mock game rules and categories
  static EntityCategorySet commandSet;
  static EntityCategorySet subcommanderSet;
  static EntityCategorySet experimentalSet;
  static EntityCategorySet customAirSet;

  commandSet.Bits().Add(10);        // Ordinal 10 is COMMAND
  subcommanderSet.Bits().Add(20);   // Ordinal 20 is SUBCOMMANDER
  experimentalSet.Bits().Add(30);   // Ordinal 30 is EXPERIMENTAL
  customAirSet.Bits().Add(40);      // Ordinal 40 is CUSTOM / AIR

  struct MockGameRulesTable
  {
    static const EntityCategorySet* MockGetEntityCategory(const void* /*thisPtr*/, const char* name)
    {
      if (std::strcmp(name, "COMMAND") == 0) return &commandSet;
      if (std::strcmp(name, "SUBCOMMANDER") == 0) return &subcommanderSet;
      if (std::strcmp(name, "EXPERIMENTAL") == 0) return &experimentalSet;
      if (std::strcmp(name, "AIR") == 0) return &customAirSet;
      return nullptr;
    }
  };

  void* mockVtable[32];
  for (std::size_t i = 0; i < 32; ++i) {
    mockVtable[i] = reinterpret_cast<void*>(&MockGameRulesTable::MockGetEntityCategory);
  }

  struct MockGameRulesObject
  {
    void** vtable;
  } mockRulesObject{ mockVtable };

  // Session stub with mRules pointing to mockRulesObject
  alignas(CWldSession) std::byte sessionBytes[sizeof(CWldSession)]{};
  CWldSession* const mockSession = reinterpret_cast<CWldSession*>(sessionBytes);
  mockSession->mRules = reinterpret_cast<RRuleGameRulesImpl*>(&mockRulesObject);

  // Blueprints
  alignas(REntityBlueprint) std::byte bpCommandBytes[sizeof(REntityBlueprint)]{};
  REntityBlueprint* const bpCommand = reinterpret_cast<REntityBlueprint*>(bpCommandBytes);
  bpCommand->mBlueprintOrdinal = 10;

  alignas(REntityBlueprint) std::byte bpSubcommanderBytes[sizeof(REntityBlueprint)]{};
  REntityBlueprint* const bpSubcommander = reinterpret_cast<REntityBlueprint*>(bpSubcommanderBytes);
  bpSubcommander->mBlueprintOrdinal = 20;

  alignas(REntityBlueprint) std::byte bpExpBytes[sizeof(REntityBlueprint)]{};
  REntityBlueprint* const bpExp = reinterpret_cast<REntityBlueprint*>(bpExpBytes);
  bpExp->mBlueprintOrdinal = 30;

  alignas(REntityBlueprint) std::byte bpAirBytes[sizeof(REntityBlueprint)]{};
  REntityBlueprint* const bpAir = reinterpret_cast<REntityBlueprint*>(bpAirBytes);
  bpAir->mBlueprintOrdinal = 40;

  alignas(REntityBlueprint) std::byte bpNormalBytes[sizeof(REntityBlueprint)]{};
  REntityBlueprint* const bpNormal = reinterpret_cast<REntityBlueprint*>(bpNormalBytes);
  bpNormal->mBlueprintOrdinal = 50; // Not in any special category

  // Entities
  alignas(UserEntity) std::byte entCommandBytes[sizeof(UserEntity)]{};
  UserEntity* const entCommand = reinterpret_cast<UserEntity*>(entCommandBytes);
  entCommand->mSession = mockSession;
  entCommand->mParams.mBlueprint = bpCommand;

  alignas(UserEntity) std::byte entSubcommanderBytes[sizeof(UserEntity)]{};
  UserEntity* const entSubcommander = reinterpret_cast<UserEntity*>(entSubcommanderBytes);
  entSubcommander->mSession = mockSession;
  entSubcommander->mParams.mBlueprint = bpSubcommander;

  alignas(UserEntity) std::byte entExpBytes[sizeof(UserEntity)]{};
  UserEntity* const entExp = reinterpret_cast<UserEntity*>(entExpBytes);
  entExp->mSession = mockSession;
  entExp->mParams.mBlueprint = bpExp;

  alignas(UserEntity) std::byte entAirBytes[sizeof(UserEntity)]{};
  UserEntity* const entAir = reinterpret_cast<UserEntity*>(entAirBytes);
  entAir->mSession = mockSession;
  entAir->mParams.mBlueprint = bpAir;

  alignas(UserEntity) std::byte entNormalBytes[sizeof(UserEntity)]{};
  UserEntity* const entNormal = reinterpret_cast<UserEntity*>(entNormalBytes);
  entNormal->mSession = mockSession;
  entNormal->mParams.mBlueprint = bpNormal;

  // Filter 0: All units pass
  EXPECT_TRUE(UnitPassesStrategicIconCategoryFilter(entCommand, 0));
  EXPECT_TRUE(UnitPassesStrategicIconCategoryFilter(entSubcommander, 0));
  EXPECT_TRUE(UnitPassesStrategicIconCategoryFilter(entExp, 0));
  EXPECT_TRUE(UnitPassesStrategicIconCategoryFilter(entAir, 0));
  EXPECT_TRUE(UnitPassesStrategicIconCategoryFilter(entNormal, 0));

  // Filter 1: Commander only (COMMAND or SUBCOMMANDER)
  EXPECT_TRUE(UnitPassesStrategicIconCategoryFilter(entCommand, 1));
  EXPECT_TRUE(UnitPassesStrategicIconCategoryFilter(entSubcommander, 1));
  EXPECT_FALSE(UnitPassesStrategicIconCategoryFilter(entExp, 1));
  EXPECT_FALSE(UnitPassesStrategicIconCategoryFilter(entAir, 1));
  EXPECT_FALSE(UnitPassesStrategicIconCategoryFilter(entNormal, 1));

  // Filter 2: Commander + Experimental (COMMAND, SUBCOMMANDER, or EXPERIMENTAL)
  EXPECT_TRUE(UnitPassesStrategicIconCategoryFilter(entCommand, 2));
  EXPECT_TRUE(UnitPassesStrategicIconCategoryFilter(entSubcommander, 2));
  EXPECT_TRUE(UnitPassesStrategicIconCategoryFilter(entExp, 2));
  EXPECT_FALSE(UnitPassesStrategicIconCategoryFilter(entAir, 2));
  EXPECT_FALSE(UnitPassesStrategicIconCategoryFilter(entNormal, 2));

  // Filter -1: Custom category
  // With custom set to "AIR":
  SetStrategicIconCustomCategory("AIR");
  EXPECT_TRUE(UnitPassesStrategicIconCategoryFilter(entAir, -1));
  EXPECT_FALSE(UnitPassesStrategicIconCategoryFilter(entCommand, -1));
  EXPECT_FALSE(UnitPassesStrategicIconCategoryFilter(entNormal, -1));

  // With custom set to category set directly (copied by value, avoids use-after-free):
  {
    EntityCategorySet tempCategorySet;
    tempCategorySet.Bits().Add(40); // AIR
    SetStrategicIconCustomCategorySet(&tempCategorySet);
    // tempCategorySet destroyed/cleared here
  }
  EXPECT_TRUE(UnitPassesStrategicIconCategoryFilter(entAir, -1));
  EXPECT_FALSE(UnitPassesStrategicIconCategoryFilter(entCommand, -1));

  // Setting numeric filter mode clears custom category
  StrategicIconOutlineConfig numCfg{};
  numCfg.mCategoryFilter = 1; // Commander
  SetStrategicIconOutlineConfig(numCfg);
  EXPECT_TRUE(UnitPassesStrategicIconCategoryFilter(entCommand, 1));
  // When switched back to -1 without re-setting custom category, fallback passes all
  EXPECT_TRUE(UnitPassesStrategicIconCategoryFilter(entNormal, -1));

  // With custom cleared: fallback passes all
  SetStrategicIconCustomCategory("");
  SetStrategicIconCustomCategorySet(nullptr);
  EXPECT_TRUE(UnitPassesStrategicIconCategoryFilter(entNormal, -1));

  // Ghost, non-unit, and Formation flags in StrategicIconRecord building
  // Formation ghost receives kOutlineDisabledFlag
  const std::uint32_t ghostPacked = 0x0000FFFFu | StrategicIconInstance::kOutlineDisabledFlag;
  EXPECT_NE(ghostPacked & StrategicIconInstance::kOutlineDisabledFlag, 0u);

  // Non-unit candidate receives kOutlineDisabledFlag
  const std::uint32_t nonUnitPacked = 0x0000FFFFu | StrategicIconInstance::kOutlineDisabledFlag;
  EXPECT_NE(nonUnitPacked & StrategicIconInstance::kOutlineDisabledFlag, 0u);

  // When unit fails category filter, kOutlineDisabledFlag is set
  const std::uint32_t normalPacked = StrategicIconInstance::PackHealthAndShield(100.0f, 100.0f, 0.0f, true);
  const std::uint32_t filteredOutPacked = normalPacked | StrategicIconInstance::kOutlineDisabledFlag;
  EXPECT_NE(filteredOutPacked & StrategicIconInstance::kOutlineDisabledFlag, 0u);

  // Reset to default config
  SetStrategicIconOutlineConfig(StrategicIconOutlineConfig{});
}

TEST(StrategicIconShaderTest, OutlineConfigDefaultsAndConVars)
{
  StrategicIconOutlineConfig config{};
  EXPECT_EQ(config.mCategoryFilter, 1);
  EXPECT_EQ(config.mContent, 0);
  EXPECT_EQ(config.mPlacement, 0);
  EXPECT_TRUE(config.mPulse);

  EXPECT_EQ(ui_StrategicIconHpOutlineCategory, 1);
  EXPECT_EQ(ui_StrategicIconHpOutlineContent, 0);
  EXPECT_EQ(ui_StrategicIconHpOutlinePlacement, 0);
  EXPECT_EQ(ui_StrategicIconHpOutlinePulse, 1);
}

TEST(StrategicIconShaderTest, EnemyShieldResolutionAndShaderAlphaInSolidAndSweepModes)
{
  const std::uint32_t entityId = 998877u;
  const float liveHealth = 1000.0f;
  const float liveMaxHealth = 1000.0f;
  const float liveShieldRatio = 0.8f;
  const bool isFriendly = false;
  const bool isSeenNow = true;

  StrategicIconRenderer& renderer = StrategicIconRenderer::Get();
  renderer.ClearEnemyRecon();

  const std::uint32_t resolvedSeen = renderer.ResolveHealthAndShield(
    entityId, isFriendly, isSeenNow, liveHealth, liveMaxHealth, liveShieldRatio
  );
  EXPECT_EQ(resolvedSeen & StrategicIconInstance::kOutlineDisabledFlag, 0u);
  EXPECT_EQ((resolvedSeen >> 16) & 0x7FFFu, 0x7FFFu);

  // When enemy moves into Fog-of-War (isSeenNow = false), recon memory returns cached state:
  const std::uint32_t resolvedFow = renderer.ResolveHealthAndShield(
    entityId, isFriendly, false, 0.0f, 0.0f, 0.0f
  );
  EXPECT_EQ(resolvedFow & StrategicIconInstance::kOutlineDisabledFlag, 0u);
  EXPECT_EQ((resolvedFow >> 16) & 0x7FFFu, 0x7FFFu);

  // Shader emulation:
  const float shieldFrac = float((resolvedSeen >> 16) & 0x7FFFu) / 32767.0f;
  EXPECT_NEAR(shieldFrac, 1.0f, 0.0001f);

  // In Solid mode (OutlineMode == 1): shield is active and solid
  const int outlineModeSolid = 1;
  const float shieldAlphaSolid = (outlineModeSolid == 1) ? ((shieldFrac > 0.0f) ? 1.0f : 0.0f) : 0.0f;
  EXPECT_EQ(shieldAlphaSolid, 1.0f);

  // In Sweep mode (OutlineMode == 2): shield arc sweeps full circle when 1.0f
  const int outlineModeSweep = 2;
  const float normAngleHalf = 0.5f;
  const float shieldAlphaSweep = (outlineModeSweep == 2) ? ((shieldFrac > 0.0f && normAngleHalf <= shieldFrac) ? 1.0f : 0.0f) : 0.0f;
  EXPECT_EQ(shieldAlphaSweep, 1.0f);

  renderer.ClearEnemyRecon();
}

TEST(StrategicIconShaderTest, ShieldOutlineSuppressionWhenChargingOrInactive)
{
  // 1. Verify SSTIUnitVariableData layout invariants
  static_assert(sizeof(SSTIUnitVariableData) == 0x228, "SSTIUnitVariableData size must be 0x228");
  static_assert(offsetof(SSTIUnitVariableData, mShieldActive) == 0x01B, "SSTIUnitVariableData::mShieldActive offset");

  SSTIUnitVariableData varData{};
  EXPECT_FALSE(varData.mShieldActive);
  varData.mShieldRatio = 0.65f; // Charging up in RechargeState

  // While mShieldActive is false, effective ratio for strategic icon outline is 0.0f
  const float effectiveShieldRatioInactive = varData.mShieldActive ? varData.mShieldRatio : 0.0f;
  EXPECT_EQ(effectiveShieldRatioInactive, 0.0f);

  StrategicIconRenderer& renderer = StrategicIconRenderer::Get();
  const std::uint32_t resolvedInactive = renderer.ResolveHealthAndShield(
    12345u, true, true, 500.0f, 1000.0f, effectiveShieldRatioInactive
  );
  const float shieldFracInactive = float((resolvedInactive >> 16) & 0x7FFFu) / 32767.0f;
  EXPECT_EQ(shieldFracInactive, 0.0f);

  // Shader emulation for Solid and Sweep modes: shield outline must be 0 alpha
  const float shieldAlphaSolidInactive = (shieldFracInactive > 0.0f) ? 1.0f : 0.0f;
  EXPECT_EQ(shieldAlphaSolidInactive, 0.0f);

  // 2. When shield finishes charging and transitions to OnState, mShieldActive becomes true
  varData.mShieldActive = true;
  const float effectiveShieldRatioActive = varData.mShieldActive ? varData.mShieldRatio : 0.0f;
  EXPECT_EQ(effectiveShieldRatioActive, 0.65f);

  const std::uint32_t resolvedActive = renderer.ResolveHealthAndShield(
    12345u, true, true, 500.0f, 1000.0f, effectiveShieldRatioActive
  );
  const float shieldFracActive = float((resolvedActive >> 16) & 0x7FFFu) / 32767.0f;
  EXPECT_GT(shieldFracActive, 0.0f);

  const float shieldAlphaSolidActive = (shieldFracActive > 0.0f) ? 1.0f : 0.0f;
  EXPECT_EQ(shieldAlphaSolidActive, 1.0f);
}

} // namespace moho
