# Strategic Icon HP and Shield Outline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Provide GPU-rendered health and shield outlines directly on strategic unit icons in Supreme Commander: Forged Alliance with 4 operating modes (Default, Color Shift, Combined, and Smart Clutter-Free), enemy shield fog-of-war compliance, and full options menu configuration.

**Architecture:** Native C++ client subsystem in `faf-re` with instanced GPU rendering in `StrategicIconRenderer` / `StrategicIconShader.h`. Unit health and shield ratios are packed into per-instance data during `CWldSessionRender`, with procedural ring/sweep geometry calculated in HLSL pixel shaders. Lua C++ bindings expose configuration atomically to the FAF UI, integrated into `fa/lua/options/options.lua` under `Interface`.

**Tech Stack:** C++20, DirectX / HLSL (StrategicIconShader.h), LuaPlus, CMake, GoogleTest.

**Spec:** [docs/specs/2026-11-15-strategic-icon-hp-outline-design.md](file:///i:/ProjectsFaf/faf-re/docs/specs/2026-11-15-strategic-icon-hp-outline-design.md)

## Global Constraints

- 100% Retail Replay & Multiplayer Compatible: Zero simulation modifications, zero desync risk (client-only presentation pass).
- Fog of War Protection: Enemy unit shields must be rendered strictly binary (100% or 0%) to prevent leaking hitpoints.
- High Performance: All outline and sweep calculations run on the GPU via instanced vertex pulling without CPU quad allocations.
- No Console-Only Features: All functionality must be configurable through `fa/lua/options/options.lua`.
- Documentation First: All ConVars must be documented in [docs/SWITCHES.md](file:///i:/ProjectsFaf/faf-re/docs/SWITCHES.md).

## Review Focus

1. **Fog of War Shield Leak:** Ensure enemy shields never display fractional damage; they must display either full 100% ring or nothing.
2. **Nan and Zero MaxHealth Division:** Zero max health or invalid units must clamp `healthFraction` to 1.0f rather than generating NaNs.
3. **Smart Mode Culling:** In Smart Mode, undamaged units with HP >= cutoff must produce zero outline pixels without disrupting the base unit icon.
4. **Shader Antialiasing:** Outlines must remain crisp and properly smoothed (`smoothstep`) across all camera zoom levels.
5. **Checksum Invariance:** Headless replay execution must produce identical checksums with outlines enabled vs disabled.

---

### Task 1: C++ Configuration Structure, ConVars, and SWITCHES.md

**Files:**
- Modify: `src/moho/sim/CWldSession.h`
- Modify: `src/moho/sim/CWldSession.cpp`
- Modify: `src/moho/console/ClientConCommands.cpp`
- Modify: `docs/SWITCHES.md`
- Test: `tests/render/StrategicIconHpOutlineConfigTests.cpp`

**Interfaces:**
- Produces: `moho::EStrategicIconOutlineMode`, `moho::StrategicIconOutlineConfig`, `GetStrategicIconOutlineConfig()`, `SetStrategicIconOutlineConfig()`.

- [ ] **Step 1: Write the failing unit test for configuration state and ConVar sync**

```cpp
// in tests/render/StrategicIconHpOutlineConfigTests.cpp
#include <gtest/gtest.h>
#include "moho/sim/CWldSession.h"

namespace moho {
  extern int ui_StrategicIconHpOutlineMode;
  extern float ui_StrategicIconHpOutlineSmartCutoff;
  extern int ui_StrategicIconHpOutlineShield;
  extern int ui_StrategicIconHpOutlinePalette;
}

TEST(StrategicIconHpOutlineConfigTest, DefaultsAndModeTransitions)
{
  using namespace moho;
  EXPECT_EQ(ui_StrategicIconHpOutlineMode, 0);
  EXPECT_FLOAT_EQ(ui_StrategicIconHpOutlineSmartCutoff, 1.0f);
  EXPECT_EQ(ui_StrategicIconHpOutlineShield, 1);
  EXPECT_EQ(ui_StrategicIconHpOutlinePalette, 0);

  StrategicIconOutlineConfig config;
  config.mMode = EStrategicIconOutlineMode::Smart;
  config.mSmartCutoff = 0.95f;
  config.mShowShield = false;
  config.mColorGood = 0xFF22C55E;

  SetStrategicIconOutlineConfig(config);
  const auto retrieved = GetStrategicIconOutlineConfig();

  EXPECT_EQ(retrieved.mMode, EStrategicIconOutlineMode::Smart);
  EXPECT_FLOAT_EQ(retrieved.mSmartCutoff, 0.95f);
  EXPECT_FALSE(retrieved.mShowShield);
  EXPECT_EQ(retrieved.mColorGood, 0xFF22C55E);
  EXPECT_EQ(ui_StrategicIconHpOutlineMode, 3);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest -R StrategicIconHpOutlineConfigTest --output-on-failure`
Expected: FAIL (types and functions undefined)

- [ ] **Step 3: Implement `StrategicIconOutlineConfig` in CWldSession and register ConVars**

In `src/moho/sim/CWldSession.h`:
```cpp
namespace moho
{
  enum class EStrategicIconOutlineMode : std::uint8_t
  {
    None = 0,
    ColorShift = 1,
    Combined = 2,
    Smart = 3,
  };

  struct StrategicIconOutlineConfig
  {
    EStrategicIconOutlineMode mMode = EStrategicIconOutlineMode::None;
    float mSmartCutoff = 1.0f;
    bool mShowShield = true;
    float mGoodCutoff = 0.65f;
    float mBadCutoff = 0.30f;
    std::uint32_t mColorGood = 0xFF22C55E;
    std::uint32_t mColorMed  = 0xFFEAB308;
    std::uint32_t mColorBad  = 0xFFEF4444;
    std::uint32_t mColorShield = 0xFF00C8FF;
  };

  StrategicIconOutlineConfig GetStrategicIconOutlineConfig() noexcept;
  void SetStrategicIconOutlineConfig(const StrategicIconOutlineConfig& config) noexcept;
}
```

In `src/moho/console/ClientConCommands.cpp`:
Register `ui_StrategicIconHpOutlineMode`, `ui_StrategicIconHpOutlineSmartCutoff`, `ui_StrategicIconHpOutlineShield`, `ui_StrategicIconHpOutlinePalette`.
In `docs/SWITCHES.md`:
Add switch entries for all 4 convars.

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest -R StrategicIconHpOutlineConfigTest --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit changes**

```bash
git add src/moho/sim/CWldSession.h src/moho/sim/CWldSession.cpp src/moho/console/ClientConCommands.cpp docs/SWITCHES.md tests/render/StrategicIconHpOutlineConfigTests.cpp
git commit -m "feat(render): add strategic icon hp outline configuration and convars"
```

---

### Task 2: Lua C++ Bindings for Outline Settings

**Files:**
- Modify: `src/moho/sim/CWldSessionLua.cpp`
- Modify: `src/moho/sim/CWldSession.h`
- Test: `tests/render/StrategicIconOutlineLuaBindingTests.cpp`

**Interfaces:**
- Consumes: `StrategicIconOutlineConfig`, `SetStrategicIconOutlineConfig()`, `GetStrategicIconOutlineConfig()`
- Produces: Lua globals `SetStrategicIconOutlineConfig(table)` and `GetStrategicIconOutlineConfig()`.

- [ ] **Step 1: Write test for Lua bindings**

```cpp
// in tests/render/StrategicIconOutlineLuaBindingTests.cpp
#include <gtest/gtest.h>
#include "lua.hpp"
#include "moho/sim/CWldSession.h"

TEST(StrategicIconOutlineLuaBindingTest, LuaTableRoundtrip)
{
  lua_State* L = luaL_newstate();
  luaL_openlibs(L);
  moho::RegisterStrategicIconOutlineLua(L);

  const char* script = R"(
    SetStrategicIconOutlineConfig({
      mode = 2,
      smart_cutoff = 0.90,
      shield = true,
      palette = "neon",
      color_good = "ff00ff66"
    })
    local cfg = GetStrategicIconOutlineConfig()
    assert(cfg.mode == 2)
    assert(cfg.smart_cutoff == 0.90)
    assert(cfg.shield == true)
  )";

  EXPECT_EQ(luaL_dostring(L, script), 0) << lua_tostring(L, -1);
  lua_close(L);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest -R StrategicIconOutlineLuaBindingTest --output-on-failure`
Expected: FAIL (RegisterStrategicIconOutlineLua not found)

- [ ] **Step 3: Implement Lua bindings in `CWldSessionLua.cpp`**

Expose:
- `SetStrategicIconOutlineConfig`: reads Lua table keys (`mode`, `smart_cutoff`, `shield`, `color_good`, etc.) and calls `SetStrategicIconOutlineConfig`.
- `GetStrategicIconOutlineConfig`: pushes Lua table with current settings.

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest -R StrategicIconOutlineLuaBindingTest --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit changes**

```bash
git add src/moho/sim/CWldSessionLua.cpp tests/render/StrategicIconOutlineLuaBindingTests.cpp
git commit -m "feat(lua): expose SetStrategicIconOutlineConfig and GetStrategicIconOutlineConfig to Lua"
```

---

### Task 3: Instance Data Packing and Fog-of-War Shield Logic

**Files:**
- Modify: `src/moho/render/StrategicIconRenderer.h`
- Modify: `src/moho/sim/CWldSessionRender.cpp`
- Test: `tests/render/StrategicIconInstancePackingTests.cpp`

**Interfaces:**
- Consumes: `UserEntity::mVariableData.mHealth`, `UserUnit::mUnitVarDat.mShieldRatio`, `icon.mIsFriendly`
- Produces: `StrategicIconInstance::mHealthShield` packed 16-bit ratios with enemy binary shield enforcement.

- [ ] **Step 1: Write test for instance packing and enemy shield privacy**

```cpp
// in tests/render/StrategicIconInstancePackingTests.cpp
#include <gtest/gtest.h>
#include "moho/render/StrategicIconRenderer.h"

namespace moho {
  std::uint32_t PackHealthAndShield(float health, float maxHealth, float shieldRatio, bool isFriendly);
}

TEST(StrategicIconInstancePackingTest, HealthClampingAndShieldFogOfWar)
{
  using namespace moho;

  // Zero max health clamps safely to 1.0 (no division by zero)
  uint32_t packedZeroMax = PackHealthAndShield(100.0f, 0.0f, 0.0f, true);
  EXPECT_EQ(packedZeroMax & 0xFFFFu, 0xFFFFu);

  // Friendly unit with 50% health and 75% shield
  uint32_t friendly = PackHealthAndShield(50.0f, 100.0f, 0.75f, true);
  EXPECT_NEAR((friendly & 0xFFFFu) / 65535.0f, 0.50f, 0.01f);
  EXPECT_NEAR(((friendly >> 16) & 0xFFFFu) / 65535.0f, 0.75f, 0.01f);

  // Enemy unit with damaged shield (30%) must be reported as 100% binary shield (0xFFFF)
  uint32_t enemyDamaged = PackHealthAndShield(40.0f, 100.0f, 0.30f, false);
  EXPECT_EQ((enemyDamaged >> 16) & 0xFFFFu, 0xFFFFu); // Full shield ring, no leak

  // Enemy unit with broken shield (0%) reports 0
  uint32_t enemyBroken = PackHealthAndShield(40.0f, 100.0f, 0.0f, false);
  EXPECT_EQ((enemyBroken >> 16) & 0xFFFFu, 0u);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest -R StrategicIconInstancePackingTest --output-on-failure`
Expected: FAIL

- [ ] **Step 3: Implement packing in `StrategicIconRenderer.h` and `CWldSessionRender.cpp`**

In `StrategicIconRenderer.h`:
Extend `StrategicIconInstance` with `std::uint32_t mHealthShield = 0U;`.
In `CWldSessionRender.cpp`:
In `BuildStrategicIconRecord`, extract entity health and shield ratio, apply `isFriendly` check, and pack into `mHealthShield`.

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest -R StrategicIconInstancePackingTest --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit changes**

```bash
git add src/moho/render/StrategicIconRenderer.h src/moho/sim/CWldSessionRender.cpp tests/render/StrategicIconInstancePackingTests.cpp
git commit -m "feat(render): pack unit health and shield with fog-of-war protection into strategic icon instance"
```

---

### Task 4: GPU HLSL Shader Implementation (`StrategicIconShader.h`)

**Files:**
- Modify: `src/moho/render/StrategicIconShader.h`
- Modify: `src/moho/render/StrategicIconRenderer.cpp`
- Test: `tests/render/StrategicIconShaderTests.cpp`

**Interfaces:**
- Consumes: `StrategicIconInstance::mHealthShield`, `cbStrategicIconOutline`
- Produces: Antialiased procedural HP outline and outer shield ring in shader output.

- [ ] **Step 1: Write shader test verifying uniform buffer and vertex packing layout**

```cpp
// in tests/render/StrategicIconShaderTests.cpp
#include <gtest/gtest.h>
#include "moho/render/StrategicIconShader.h"

TEST(StrategicIconShaderTest, ConstantBufferAlignmentAndInstanceStride)
{
  using namespace moho;
  EXPECT_EQ(sizeof(StrategicIconOutlineConstants) % 16, 0); // HLSL 16-byte alignment
  EXPECT_EQ(sizeof(StrategicIconInstance), 20); // 16 bytes base + 4 bytes mHealthShield
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest -R StrategicIconShaderTest --output-on-failure`
Expected: FAIL

- [ ] **Step 3: Implement outline procedural rendering in HLSL shader**

In `StrategicIconShader.h`:
1. Add `StrategicIconOutlineConstants` matching `cbStrategicIconOutline`.
2. In the pixel shader:
   - Compute normalized polar coordinates $r$ and $\theta = \text{atan2}(u, -v)$.
   - Draw HP outline in band $[0.75, 0.88]$:
     - In Mode 1: always drawn, color interpolated based on health.
     - In Mode 2: sweep arc gated by $\theta \le \text{healthFraction} \times 2\pi$.
     - In Mode 3: discard if $\text{healthFraction} \ge \text{smartCutoff}$.
   - Draw shield ring in band $[0.90, 1.00]$ if enabled and shield active.
   - Smooth edge transitions with `smoothstep`.
In `StrategicIconRenderer.cpp`:
Upload constants to GPU on frame preparation.

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest -R StrategicIconShaderTest --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit changes**

```bash
git add src/moho/render/StrategicIconShader.h src/moho/render/StrategicIconRenderer.cpp tests/render/StrategicIconShaderTests.cpp
git commit -m "feat(render): implement procedural hp and shield outline in strategic icon shader"
```

---

### Task 5: Options Menu Integration in FAF Lua UI

**Files:**
- Modify: `I:\ProjectsFaf\fa\lua\options\options.lua`
- Test: `tests/ui/IconOutlineOptionsTests.lua` (via `faf_lua_tests.exe`)

**Interfaces:**
- Consumes: `SetStrategicIconOutlineConfig`
- Produces: UI options entries under `F10 -> Options -> Interface`.

- [ ] **Step 1: Write Lua test for options table registration**

```lua
-- in tests/ui/IconOutlineOptionsTests.lua
local options = import('/lua/options/options.lua')

function Test_IconOutlineOptionsRegistered()
    local foundMode = false
    local foundCutoff = false
    local foundShield = false
    local foundPalette = false

    for _, opt in options.options.interface.items do
        if opt.id == 'strategic_icon_outline_mode' then foundMode = true end
        if opt.id == 'strategic_icon_outline_smart_cutoff' then foundCutoff = true end
        if opt.id == 'strategic_icon_outline_shield' then foundShield = true end
        if opt.id == 'strategic_icon_outline_palette' then foundPalette = true end
    end

    assert(foundMode, "strategic_icon_outline_mode not found in options")
    assert(foundCutoff, "strategic_icon_outline_smart_cutoff not found in options")
    assert(foundShield, "strategic_icon_outline_shield not found in options")
    assert(foundPalette, "strategic_icon_outline_palette not found in options")
end
```

- [ ] **Step 2: Run test to verify it fails**

Run: `.\bin\x64\Release\faf_lua_tests.exe --test tests/ui/IconOutlineOptionsTests.lua`
Expected: FAIL

- [ ] **Step 3: Add options entries in `fa/lua/options/options.lua`**

Add items to the `Interface` group with descriptive labels, tooltips, and callback hook calling `SetStrategicIconOutlineConfig`.

- [ ] **Step 4: Run test to verify it passes**

Run: `.\bin\x64\Release\faf_lua_tests.exe --test tests/ui/IconOutlineOptionsTests.lua`
Expected: PASS

- [ ] **Step 5: Commit changes**

```bash
git -C I:\ProjectsFaf\fa add lua/options/options.lua
git -C I:\ProjectsFaf\fa commit -m "feat(ui): add strategic icon hp outline settings to interface options"
```

---

### Task 6: Headless Replay Checksum Verification & Tracy Performance Profiling

**Files:**
- Test: `tests/test_replay_28013017_bidirectional.ps1`
- Test: `scripts/perf/run_benchmark_tracy_capture.py`

**Interfaces:**
- Runs real simulation verification and collects GPU/CPU execution metrics.

- [ ] **Step 1: Run headless replay checksum test**

Execute headless simulation run with outline mode disabled vs enabled:
```powershell
.\bin\x64\Release\headless_x64.exe --replay tests/replays/28013017.fafreplay --exit-at 2000
```
Verify byte-for-byte checksum identity across all 2000 simulation beats.

- [ ] **Step 2: Collect Tracy capture and verify GPU frame time overhead**

Run benchmark with 2000 active units:
```powershell
python scripts/perf/launch_for_tracy.py --scenario setons_clutch_2000_units
```
Verify `StrategicIconRenderer` GPU pass duration is $< 0.15\text{ ms}$.

- [ ] **Step 3: Commit verification test scripts and documentation**

```bash
git add tests/ docs/
git commit -m "test(render): add verification evidence for strategic icon hp outline determinism and performance"
```
