#pragma once

// Engine addition, not in the shipped binary: GPU-driven strategic icons
// (`ren_GpuIcons`).
//
// The CPU path (`CWldSession::RenderStrategicIcons` -> `RenderUnitIcon`)
// builds four 24-byte vertices and six indices per icon quad through
// `CD3DPrimBatcher` - over 100,000 quads per frame with tens of thousands of
// units zoomed out - and uploads all of them every frame. Here each icon is one
// 16-byte record (screen point, tint, the atlas slots of its up to four layers)
// in a persistent structured buffer; only the 4 KB blocks of records that
// changed since the previous frame of the same view are uploaded, and one
// instanced draw expands the quads in the vertex shader (StrategicIconShader.h).
//
// The textures stay in the prim batcher's own atlas (`CD3DTextureBatcher`),
// with the same UV rectangles `CD3DPrimBatcher::SetTexture` computes, so the
// sampled texels are the same. Records keep the CPU path's order (ground, air,
// high-priority, selected; underlay, base, paused, stunned within an icon), and
// a draw rasterises its primitives in order, so the blended result is the same.

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

#include <memory>
#include "moho/math/VMatrix4.h"
#include "moho/render/GpuArrayStream.h"

namespace gpg::gal
{
  class GpuBuffer;
  class GpuProgram;
  class Texture;
} // namespace gpg::gal

namespace moho
{
  class CD3DBatchTexture;
  class CD3DPrimBatcher;

  /// `ren_GpuIcons`: draw strategic icons through StrategicIconRenderer when the backend can.
  extern int ren_GpuIcons;
  /// `ren_GpuIconClassify`: also classify them on the GPU (with `ren_GpuIcons` and `ui_IconRecords`).
  extern int ren_GpuIconClassify;

  struct StrategicIconSourceRecord;
  struct StrategicIconClassifyConstants;

  struct DrawConstants
  {
    VMatrix4 composite;
    std::uint32_t outlineMode = 0U;
    float smartCutoff = 0.0f;
    std::uint32_t outlineContent = 0U; // 0=HP+Shield, 1=HP Only, 2=Shield Only
    float goodCutoff = 0.65f;
    float badCutoff = 0.30f;
    std::uint32_t colorGood = 0xFF22C55Eu;
    std::uint32_t colorMed  = 0xFFEAB308u;
    std::uint32_t colorBad  = 0xFFEF4444u;
    std::uint32_t colorShield = 0xFF00C8FFu;
    std::uint32_t outlinePlacement = 0U; // 0=Full, 1=Top, 2=Bottom, 3=Split (Shield Top, HP Bottom)
    std::uint32_t pulseEnabled = 1U;
    float animTime = 0.0f;
  };
  static_assert(sizeof(DrawConstants) == 112, "DrawConstants must be 112 bytes (7 float4s)");
  static_assert(sizeof(DrawConstants) % 16 == 0, "constant blocks are whole float4s");

  /** One icon as the GPU reads it (`IconInstance` in StrategicIconShader.h). */
  struct StrategicIconInstance
  {
    std::uint32_t mPosition = 0U; ///< x low 16 bits, y high 16 bits, signed whole pixels
    std::uint32_t mColor = 0U;    ///< ARGB tint of the base icon
    std::uint32_t mLayers01 = 0xFFFFFFFFU;
    std::uint32_t mLayers23 = 0xFFFFFFFFU;
    std::uint32_t mHealthShield = 0U; ///< low 16 bits: health ratio (unorm16), bits 16..30: shield ratio (unorm15), bit 31: outline disabled flag

    static constexpr std::uint16_t kNoLayer = 0xFFFFU;
    static constexpr std::uint32_t kLayerCount = 5U; ///< underlay, base, paused, stunned, outline
    static constexpr std::uint32_t kOutlineDisabledFlag = 0x80000000U;

    void SetPosition(float screenX, float screenY) noexcept;
    void SetLayer(std::uint32_t layer, std::uint16_t slot) noexcept;

    static std::uint32_t PackHealthAndShield(
      float health,
      float maxHealth,
      float shieldRatio,
      bool isFriendly
    ) noexcept;
  };
  static_assert(sizeof(StrategicIconInstance) == 20, "StrategicIconInstance must match IconInstance");

  class StrategicIconRenderer
  {
  public:
    [[nodiscard]] static StrategicIconRenderer& Get();

    /** `ren_GpuIcons` is on and the active backend can draw this path (creates it on first use). */
    [[nodiscard]] bool IsActive();

    /**
     * The atlas slot of `texture`, or `StrategicIconInstance::kNoLayer` when it
     * has none yet. Read-only, so workers may call it while nothing registers.
     */
    [[nodiscard]] std::uint16_t FindSlot(const CD3DBatchTexture* texture) const noexcept;

    /** Main thread: the slot of `texture`, registering it on first use. */
    [[nodiscard]] std::uint16_t AddSlot(const std::shared_ptr<CD3DBatchTexture>& texture);

    /**
     * Main thread, after the records of a frame are built: draws `instances` for
     * the view `viewKey` through `batcher`'s atlas and matrices. Returns false,
     * having drawn nothing, when the atlas cannot hold the icon textures or a
     * texture is too large for it; the caller then draws through the CPU path.
     */
    [[nodiscard]] bool
    Draw(CD3DPrimBatcher& batcher, const void* viewKey, std::span<const StrategicIconInstance> instances);

    /**
     * `IsActive()`, `ren_GpuIconClassify` is on and the backend has compute
     * shaders and indirect draws (creates the classification programs on first use).
     */
    [[nodiscard]] bool IsClassifyActive();

    /**
     * Main thread: classifies `sources` on the GPU (StrategicIconClassifyShader.h)
     * against `constants` (whose `mCount` is set here) and `selectionBits` (one
     * bit per source), and draws the records it produces for the view
     * `viewKey` with one indirect draw. Each array is compared and uploaded
     * again only when its version differs from the one this view last drew
     * with. Returns false, having drawn nothing, when the renderer declines;
     * the caller then classifies on the CPU.
     */
    [[nodiscard]] bool DrawClassified(
      CD3DPrimBatcher& batcher,
      const void* viewKey,
      std::span<const StrategicIconSourceRecord> sources,
      std::uint64_t sourcesVersion,
      std::span<const std::uint32_t> selectionBits,
      std::uint64_t selectionVersion,
      const StrategicIconClassifyConstants& constants
    );

    /**
     * Diagnostics (FAF_ICON_GPU_CHECK): the records the last `DrawClassified`
     * drew, in draw order, read back from the GPU. Waits for the GPU; false when
     * the backend cannot read buffers back.
     */
    [[nodiscard]] bool ReadBackClassified(std::vector<StrategicIconInstance>& out);

    struct EnemyReconData
    {
      float mHealth = 0.0f;
      float mMaxHealth = 0.0f;
      float mShieldRatio = 0.0f;
    };

    /**
     * Resolves and packs health and shield state for an entity's strategic icon outline,
     * maintaining Fog-of-War recon memory for enemy units.
     *
     * Rules:
     * 1. Friendly/allied entities: always returns live health and shield state (isFriendly = true).
     * 2. Non-friendly entities seen in direct optical LOS (isSeenNow = true):
     *    - Updates the cached enemy recon record for entityId.
     *    - Returns live health and shield state (isFriendly = false).
     * 3. Non-friendly entities NOT seen in direct optical LOS (isSeenNow = false):
     *    - If previously scouted (exists in enemy recon cache):
     *      returns the frozen cached health and shield state (isFriendly = false).
     *      Artillery or blind fire into fog will NOT update this, preventing intel leaks!
     *    - If never scouted (radar blip only / not in cache):
     *      returns 0x0000FFFFu | kOutlineDisabledFlag (outline disabled).
     */
    [[nodiscard]] std::uint32_t ResolveHealthAndShield(
      std::uint32_t entityId,
      bool isFriendly,
      bool isSeenNow,
      float liveHealth,
      float liveMaxHealth,
      float liveShieldRatio
    );

    /** Clears all cached enemy recon health/shield states (e.g. on session start/exit or focus army change). */
    void ClearEnemyRecon();

    /** Removes a specific entity from enemy recon cache (e.g. on entity orphan/deletion). */
    void RemoveEnemyRecon(std::uint32_t entityId);

  private:
    struct Slot
    {
      std::shared_ptr<CD3DBatchTexture> mTexture;   // keeps the address from being reused
      float mUv[4] = {};                            // origin and extent in the atlas
      float mHalfSize[2] = {};
      float mUnused[2] = {};
    };

    struct EnemyReconState
    {
      std::mutex mMutex;
      std::unordered_map<std::uint32_t, EnemyReconData> mMap;
    };

    /** The GPU copy of one view's records, kept between frames. */
    struct ViewStream
    {
      const void* mViewKey = nullptr;
      std::uint64_t mLastUsedFrame = 0U;
      GpuArrayStream mRecords{"StrategicIcons", sizeof(StrategicIconInstance)};
      // `DrawClassified`: the view's sources and selection bits, and the versions they hold.
      GpuArrayStream mSources{"StrategicIconSources", 64U};
      GpuArrayStream mSelection{"StrategicIconSelection", sizeof(std::uint32_t)};
      std::uint64_t mSourcesVersion = ~0ULL;
      std::uint64_t mSelectionVersion = ~0ULL;
    };

    bool CreateResources();
    bool CreateClassifyResources();
    /** The classification work buffers, grown to hold `count` sources. */
    bool EnsureClassifyCapacity(std::uint32_t count);
    /** Re-reads every slot's atlas rectangle; false when the atlas cannot hold them. */
    bool RefreshAtlasSlots(CD3DPrimBatcher& batcher);
    void UploadSlots();
    /** Everything both draws need first: the atlas slots, the batcher flushed, the atlas texture. */
    [[nodiscard]] gpg::gal::Texture* PrepareDraw(CD3DPrimBatcher& batcher);
    ViewStream& StreamFor(const void* viewKey);

    bool mInitialized = false;
    bool mSupported = false;
    std::uint64_t mFrame = 0U;
    std::shared_ptr<gpg::gal::GpuProgram> mProgram;
    std::shared_ptr<gpg::gal::GpuBuffer> mSlotBuffer;
    std::uint32_t mSlotCapacity = 0U;
    bool mSlotsDirty = false;
    float mUploadedIconScale = 1.0f; // `ui_StrategicIconScale` the slot buffer was built with
    std::vector<Slot> mSlots;
    std::unordered_map<const CD3DBatchTexture*, std::uint16_t> mSlotByTexture;
    std::vector<ViewStream> mStreams;

    bool mClassifyInitialized = false;
    bool mClassifySupported = false;
    std::shared_ptr<gpg::gal::GpuProgram> mClassifyProgram;
    std::shared_ptr<gpg::gal::GpuProgram> mCompactProgram;
    std::uint32_t mClassifyCapacity = 0U;
    std::shared_ptr<gpg::gal::GpuBuffer> mStaged; // one record per source
    std::shared_ptr<gpg::gal::GpuBuffer> mRunOf;  // one run per source
    std::shared_ptr<gpg::gal::GpuBuffer> mOutput; // the records in draw order
    std::shared_ptr<gpg::gal::GpuBuffer> mDrawArgs;
    std::uint32_t mLastClassifiedSources = 0U;

    std::unique_ptr<EnemyReconState> mEnemyReconState = std::make_unique<EnemyReconState>();
  };
} // namespace moho
