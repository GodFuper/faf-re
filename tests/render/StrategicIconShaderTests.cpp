#include <gtest/gtest.h>
#include <string_view>
#include <d3dcompiler.h>
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

} // namespace moho
