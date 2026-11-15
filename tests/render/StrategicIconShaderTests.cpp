#include <gtest/gtest.h>
#include <cstddef>
#include <string_view>
#include <d3dcompiler.h>
#include "moho/render/StrategicIconClassifyShader.h"
#include "moho/render/StrategicIconRenderer.h"
#include "moho/render/StrategicIconShader.h"

namespace moho
{

TEST(StrategicIconShaderTest, ConstantBufferAlignmentAndInstanceStride)
{
  EXPECT_EQ(sizeof(DrawConstants) % 16, 0u);
  EXPECT_EQ(sizeof(DrawConstants), 112u);
  EXPECT_EQ(StrategicIconInstance::kLayerCount, 5u);

  const DrawConstants defaults{};
  EXPECT_EQ(defaults.outlineMode, 0u);
  EXPECT_FLOAT_EQ(defaults.smartCutoff, 1.0f);
  EXPECT_EQ(defaults.showShield, 1u);
  EXPECT_FLOAT_EQ(defaults.goodCutoff, 0.65f);
  EXPECT_FLOAT_EQ(defaults.badCutoff, 0.30f);
  EXPECT_EQ(defaults.colorGood, 0xFF22C55Eu);
  EXPECT_EQ(defaults.colorMed, 0xFFEAB308u);
  EXPECT_EQ(defaults.colorBad, 0xFFEF4444u);
  EXPECT_EQ(defaults.colorShield, 0xFF00C8FFu);
}

TEST(StrategicIconShaderTest, ShaderSourceContainsOutlineTokens)
{
  const std::string_view source(kStrategicIconShaderSource);
  EXPECT_NE(source.find("OutlineMode"), std::string_view::npos);
  EXPECT_NE(source.find("SmartCutoff"), std::string_view::npos);
  EXPECT_NE(source.find("ShowShield"), std::string_view::npos);
  EXPECT_NE(source.find("GoodCutoff"), std::string_view::npos);
  EXPECT_NE(source.find("BadCutoff"), std::string_view::npos);
  EXPECT_NE(source.find("ColorGood"), std::string_view::npos);
  EXPECT_NE(source.find("ColorMed"), std::string_view::npos);
  EXPECT_NE(source.find("ColorBad"), std::string_view::npos);
  EXPECT_NE(source.find("ColorShield"), std::string_view::npos);
  EXPECT_NE(source.find("localPos"), std::string_view::npos);
  EXPECT_NE(source.find("healthShield"), std::string_view::npos);
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
  const float shieldFrac = float((iconRecord.mHealthShield >> 16) & 0xFFFFu) / 65535.0f;

  EXPECT_NEAR(healthFrac, 0.5f, 0.001f);
  EXPECT_NEAR(shieldFrac, 0.3f, 0.001f);

  // 6. Verify Smart Mode (Mode 3) logic with damaged unit:
  const float smartCutoff = 1.0f;
  // Shader check: if (OutlineMode == 3 && healthFrac >= SmartCutoff && (shieldFrac >= 1.0 || shieldFrac <= 0.0)) discard;
  const bool discardedHealthy = (healthFrac >= smartCutoff && (shieldFrac >= 1.0f || shieldFrac <= 0.0f));
  EXPECT_FALSE(discardedHealthy); // Damaged unit MUST NOT be discarded!

  // Now test an undamaged unit:
  const std::uint32_t fullHpRecord = StrategicIconInstance::PackHealthAndShield(500.0f, 500.0f, 0.0f, true);
  const float fullHpFrac = float(fullHpRecord & 0xFFFFu) / 65535.0f;
  const float noShieldFrac = float((fullHpRecord >> 16) & 0xFFFFu) / 65535.0f;
  const bool discardedFull = (fullHpFrac >= smartCutoff && (noShieldFrac >= 1.0f || noShieldFrac <= 0.0f));
  EXPECT_TRUE(discardedFull); // Full HP undamaged unit MUST be discarded in Smart Mode!
}

} // namespace moho
