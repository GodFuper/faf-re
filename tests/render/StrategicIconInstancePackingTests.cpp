#include <gtest/gtest.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "moho/render/StrategicIconClassifyShader.h"
#include "moho/render/StrategicIconRenderer.h"

namespace moho
{

TEST(StrategicIconInstancePackingTest, StructSizeIs20Bytes)
{
  EXPECT_EQ(sizeof(StrategicIconInstance), 20u);
}

TEST(StrategicIconInstancePackingTest, SourceRecordSizeIs64BytesAndHasHealthShield)
{
  EXPECT_EQ(sizeof(StrategicIconSourceRecord), 64u);
  StrategicIconSourceRecord record{};
  EXPECT_EQ(record.mHealthShield, 0x0000FFFFu);
  EXPECT_EQ(record.mUnused, 0u);
  EXPECT_EQ(offsetof(StrategicIconSourceRecord, mHealthShield), 56u);
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
  // Friendly unit with damaged shield preserves fractional value in 15 bits (0..32767)
  const std::uint32_t packedFriendly = StrategicIconInstance::PackHealthAndShield(100.0f, 100.0f, 0.5f, true);
  EXPECT_EQ(packedFriendly & 0xFFFFu, 0xFFFFu); // 100% health
  const std::uint32_t shieldHalf = packedFriendly >> 16;
  EXPECT_TRUE(shieldHalf == 16383u || shieldHalf == 16384u);

  // Damaged shield 0.35f
  const std::uint32_t packed35 = StrategicIconInstance::PackHealthAndShield(100.0f, 100.0f, 0.35f, true);
  const std::uint32_t expectedShield35 = static_cast<std::uint32_t>(std::round(0.35f * 32767.0f));
  EXPECT_EQ(packed35 >> 16, expectedShield35);
  EXPECT_NE(packed35 >> 16, 0x7FFFu);
  EXPECT_NE(packed35 >> 16, 0u);
}

TEST(StrategicIconInstancePackingTest, EnemyShieldIsStrictlyBinaryFogOfWar)
{
  // Enemy unit with damaged shield (e.g. 0.35f) returns binary full shield (0x7FFFu in 15 bits, leaving bit 31 clear)
  const std::uint32_t packedEnemyDamaged = StrategicIconInstance::PackHealthAndShield(100.0f, 100.0f, 0.35f, false);
  EXPECT_EQ(packedEnemyDamaged & 0xFFFFu, 0xFFFFu);   // 100% health
  EXPECT_EQ(packedEnemyDamaged >> 16, 0x7FFFu);       // Binary full shield (15-bit)
  EXPECT_EQ(packedEnemyDamaged, 0x7FFFFFFFu);
  EXPECT_EQ(packedEnemyDamaged & StrategicIconInstance::kOutlineDisabledFlag, 0u);

  // Even small shield ratio > 0.0f returns binary full shield
  const std::uint32_t packedEnemyTiny = StrategicIconInstance::PackHealthAndShield(50.0f, 100.0f, 0.001f, false);
  EXPECT_EQ(packedEnemyTiny >> 16, 0x7FFFu);

  // Enemy unit with depleted shield (0.0f) returns 0 shield
  const std::uint32_t packedEnemyDepleted = StrategicIconInstance::PackHealthAndShield(100.0f, 100.0f, 0.0f, false);
  EXPECT_EQ(packedEnemyDepleted & 0xFFFFu, 0xFFFFu);
  EXPECT_EQ(packedEnemyDepleted >> 16, 0u);
  EXPECT_EQ(packedEnemyDepleted, 0x0000FFFFu);

  // Enemy unit with negative shield returns 0 shield
  const std::uint32_t packedEnemyNegative = StrategicIconInstance::PackHealthAndShield(100.0f, 100.0f, -0.5f, false);
  EXPECT_EQ(packedEnemyNegative >> 16, 0u);
}

TEST(StrategicIconInstancePackingTest, FriendlyUnitsAlwaysReportLiveHealthAndShield)
{
  StrategicIconRenderer& renderer = StrategicIconRenderer::Get();
  renderer.ClearEnemyRecon();

  const std::uint32_t friendlyId = 1001u;
  // Friendly unit seen in LOS
  const std::uint32_t packedSeen = renderer.ResolveHealthAndShield(
    friendlyId,
    true,  // isFriendly
    true,  // isSeenNow
    400.0f, 1000.0f, 0.5f
  );
  EXPECT_EQ(packedSeen & 0xFFFFu, static_cast<std::uint32_t>(std::round(0.4f * 65535.0f)));
  EXPECT_EQ((packedSeen >> 16) & 0x7FFFu, static_cast<std::uint32_t>(std::round(0.5f * 32767.0f)));
  EXPECT_EQ(packedSeen & StrategicIconInstance::kOutlineDisabledFlag, 0u);

  // Friendly unit in fog of war still reports live telemetry
  const std::uint32_t packedFog = renderer.ResolveHealthAndShield(
    friendlyId,
    true,  // isFriendly
    false, // isSeenNow (in fog)
    300.0f, 1000.0f, 0.25f
  );
  EXPECT_EQ(packedFog & 0xFFFFu, static_cast<std::uint32_t>(std::round(0.3f * 65535.0f)));
  EXPECT_EQ((packedFog >> 16) & 0x7FFFu, static_cast<std::uint32_t>(std::round(0.25f * 32767.0f)));
}

TEST(StrategicIconInstancePackingTest, EnemyInFogOfWarFreezesLastKnownHealthAndShield)
{
  StrategicIconRenderer& renderer = StrategicIconRenderer::Get();
  renderer.ClearEnemyRecon();

  const std::uint32_t enemyId = 2002u;

  // 1. Enemy scouted in direct LOS with 80% HP and active shield
  const std::uint32_t scouted = renderer.ResolveHealthAndShield(
    enemyId,
    false, // isFriendly
    true,  // isSeenNow (direct optical LOS)
    800.0f, 1000.0f, 1.0f
  );
  const std::uint32_t expectedHp80 = static_cast<std::uint32_t>(std::round(0.8f * 65535.0f));
  EXPECT_EQ(scouted & 0xFFFFu, expectedHp80);
  EXPECT_EQ((scouted >> 16) & 0x7FFFu, 0x7FFFu); // Enemy binary full shield
  EXPECT_EQ(scouted & StrategicIconInstance::kOutlineDisabledFlag, 0u);

  // 2. Enemy retreats into Fog of War (isSeenNow = false).
  // Live health drops to 20% due to blind artillery fire, shield drops to 0%.
  const std::uint32_t fogHit = renderer.ResolveHealthAndShield(
    enemyId,
    false, // isFriendly
    false, // isSeenNow (in fog of war!)
    200.0f, 1000.0f, 0.0f
  );

  // CRITICAL: The outline must NOT reveal the new 20% HP or broken shield!
  // It MUST match the last scouted state (80% HP and active shield).
  EXPECT_EQ(fogHit & 0xFFFFu, expectedHp80);
  EXPECT_EQ((fogHit >> 16) & 0x7FFFu, 0x7FFFu);
  EXPECT_EQ(fogHit, scouted);

  // 3. A scout acquires direct LOS again (isSeenNow = true).
  // Now it discovers the damaged state: 20% HP, shield depleted.
  const std::uint32_t scoutedAgain = renderer.ResolveHealthAndShield(
    enemyId,
    false, // isFriendly
    true,  // isSeenNow
    200.0f, 1000.0f, 0.0f
  );
  const std::uint32_t expectedHp20 = static_cast<std::uint32_t>(std::round(0.2f * 65535.0f));
  EXPECT_EQ(scoutedAgain & 0xFFFFu, expectedHp20);
  EXPECT_EQ((scoutedAgain >> 16) & 0x7FFFu, 0u); // Shield is 0

  // 4. Scout dies, enemy is back in fog. Live health recovers to 25% (regeneration).
  const std::uint32_t backInFog = renderer.ResolveHealthAndShield(
    enemyId,
    false, // isFriendly
    false, // isSeenNow
    250.0f, 1000.0f, 0.0f
  );
  // Freezes at 20% (last seen state), ignoring unseen regeneration.
  EXPECT_EQ(backInFog & 0xFFFFu, expectedHp20);
  EXPECT_EQ((backInFog >> 16) & 0x7FFFu, 0u);
}

TEST(StrategicIconInstancePackingTest, EnemyUnscoutedInFogReturnsDisabledOutline)
{
  StrategicIconRenderer& renderer = StrategicIconRenderer::Get();
  renderer.ClearEnemyRecon();

  const std::uint32_t unscoutedEnemyId = 3003u;

  // Unscouted enemy (only seen on radar, never in optical LOS)
  const std::uint32_t unscouted = renderer.ResolveHealthAndShield(
    unscoutedEnemyId,
    false, // isFriendly
    false, // isSeenNow = false
    500.0f, 1000.0f, 1.0f
  );

  // Outline MUST be disabled
  EXPECT_NE(unscouted & StrategicIconInstance::kOutlineDisabledFlag, 0u);
}

TEST(StrategicIconInstancePackingTest, RemoveAndClearEnemyReconResetsCache)
{
  StrategicIconRenderer& renderer = StrategicIconRenderer::Get();
  renderer.ClearEnemyRecon();

  const std::uint32_t enemyA = 4001u;
  const std::uint32_t enemyB = 4002u;

  // Scout both
  (void)renderer.ResolveHealthAndShield(enemyA, false, true, 500.0f, 1000.0f, 0.0f);
  (void)renderer.ResolveHealthAndShield(enemyB, false, true, 800.0f, 1000.0f, 0.0f);

  // Remove enemyA (e.g. dead/orphaned)
  renderer.RemoveEnemyRecon(enemyA);

  // enemyA in fog now behaves as unscouted (outline disabled)
  const std::uint32_t removedA = renderer.ResolveHealthAndShield(enemyA, false, false, 500.0f, 1000.0f, 0.0f);
  EXPECT_NE(removedA & StrategicIconInstance::kOutlineDisabledFlag, 0u);

  // enemyB in fog is still cached
  const std::uint32_t keptB = renderer.ResolveHealthAndShield(enemyB, false, false, 500.0f, 1000.0f, 0.0f);
  EXPECT_EQ(keptB & StrategicIconInstance::kOutlineDisabledFlag, 0u);
  EXPECT_EQ(keptB & 0xFFFFu, static_cast<std::uint32_t>(std::round(0.8f * 65535.0f)));

  // Clear all
  renderer.ClearEnemyRecon();
  const std::uint32_t clearedB = renderer.ResolveHealthAndShield(enemyB, false, false, 500.0f, 1000.0f, 0.0f);
  EXPECT_NE(clearedB & StrategicIconInstance::kOutlineDisabledFlag, 0u);
}

} // namespace moho
