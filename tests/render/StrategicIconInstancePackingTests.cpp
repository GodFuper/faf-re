#include <gtest/gtest.h>
#include <cmath>
#include <cstdint>
#include "moho/render/StrategicIconRenderer.h"

namespace moho
{

TEST(StrategicIconInstancePackingTest, StructSizeIs20Bytes)
{
  EXPECT_EQ(sizeof(StrategicIconInstance), 20u);
}

TEST(StrategicIconInstancePackingTest, ClampsZeroAndNegativeMaxHealthToFull)
{
  // Clamping zero max health to 1.0f (no division by zero / NaN)
  const std::uint32_t packedZeroMax = StrategicIconInstance::PackHealthAndShield(0.0f, 0.0f, 0.0f, true);
  EXPECT_EQ(packedZeroMax & 0xFFFFu, 0xFFFFu); // Full health in low 16 bits
  EXPECT_EQ(packedZeroMax >> 16, 0u);          // 0 shield in high 16 bits
  EXPECT_EQ(packedZeroMax, 0x0000FFFFu);

  const std::uint32_t packedNegMax = StrategicIconInstance::PackHealthAndShield(50.0f, -100.0f, 0.0f, true);
  EXPECT_EQ(packedNegMax, 0x0000FFFFu);
}

TEST(StrategicIconInstancePackingTest, ClampsHealthBoundaries)
{
  // Clamping negative health to 0.0f
  const std::uint32_t packedNegative = StrategicIconInstance::PackHealthAndShield(-10.0f, 100.0f, 0.0f, true);
  EXPECT_EQ(packedNegative & 0xFFFFu, 0u);
  EXPECT_EQ(packedNegative, 0u);

  // Clamping over 1.0 health to 1.0f
  const std::uint32_t packedOverMax = StrategicIconInstance::PackHealthAndShield(150.0f, 100.0f, 0.0f, true);
  EXPECT_EQ(packedOverMax & 0xFFFFu, 0xFFFFu);
  EXPECT_EQ(packedOverMax, 0x0000FFFFu);

  // Normal fractional health (e.g. 50%)
  const std::uint32_t packedHalf = StrategicIconInstance::PackHealthAndShield(50.0f, 100.0f, 0.0f, true);
  const std::uint32_t healthHalf = packedHalf & 0xFFFFu;
  // 0.5 * 65535 = 32767.5 -> rounded to 32768 (0x8000)
  EXPECT_TRUE(healthHalf == 0x8000u || healthHalf == 32767u);
}

TEST(StrategicIconInstancePackingTest, FriendlyShieldPreservesFractionalValue)
{
  // Friendly unit with damaged shield preserves fractional value
  const std::uint32_t packedFriendly = StrategicIconInstance::PackHealthAndShield(100.0f, 100.0f, 0.5f, true);
  EXPECT_EQ(packedFriendly & 0xFFFFu, 0xFFFFu); // 100% health
  const std::uint32_t shieldHalf = packedFriendly >> 16;
  EXPECT_TRUE(shieldHalf == 0x8000u || shieldHalf == 32767u);

  // Damaged shield 0.35f
  const std::uint32_t packed35 = StrategicIconInstance::PackHealthAndShield(100.0f, 100.0f, 0.35f, true);
  const std::uint32_t expectedShield35 = static_cast<std::uint32_t>(std::round(0.35f * 65535.0f));
  EXPECT_EQ(packed35 >> 16, expectedShield35);
  EXPECT_NE(packed35 >> 16, 0xFFFFu);
  EXPECT_NE(packed35 >> 16, 0u);
}

TEST(StrategicIconInstancePackingTest, EnemyShieldIsStrictlyBinaryFogOfWar)
{
  // Enemy unit with damaged shield (e.g. 0.35f) returns binary full shield (0xFFFFu in high 16 bits)
  const std::uint32_t packedEnemyDamaged = StrategicIconInstance::PackHealthAndShield(100.0f, 100.0f, 0.35f, false);
  EXPECT_EQ(packedEnemyDamaged & 0xFFFFu, 0xFFFFu);   // 100% health
  EXPECT_EQ(packedEnemyDamaged >> 16, 0xFFFFu);       // Binary full shield
  EXPECT_EQ(packedEnemyDamaged, 0xFFFFFFFFu);

  // Even small shield ratio > 0.0f returns binary full shield
  const std::uint32_t packedEnemyTiny = StrategicIconInstance::PackHealthAndShield(50.0f, 100.0f, 0.001f, false);
  EXPECT_EQ(packedEnemyTiny >> 16, 0xFFFFu);

  // Enemy unit with depleted shield (0.0f) returns 0 shield
  const std::uint32_t packedEnemyDepleted = StrategicIconInstance::PackHealthAndShield(100.0f, 100.0f, 0.0f, false);
  EXPECT_EQ(packedEnemyDepleted & 0xFFFFu, 0xFFFFu);
  EXPECT_EQ(packedEnemyDepleted >> 16, 0u);
  EXPECT_EQ(packedEnemyDepleted, 0x0000FFFFu);

  // Enemy unit with negative shield returns 0 shield
  const std::uint32_t packedEnemyNegative = StrategicIconInstance::PackHealthAndShield(100.0f, 100.0f, -0.5f, false);
  EXPECT_EQ(packedEnemyNegative >> 16, 0u);
}

} // namespace moho
