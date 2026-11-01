#pragma once

#include "gpg/core/algorithms/MD5.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace moho
{
  /**
   * Represents an in-memory simulation snapshot taken during replay playback.
   */
  struct ReplaySnapshot
  {
    std::int32_t beat{0};
    std::uint64_t streamOffset{0};
    gpg::MD5Digest simHash{};
    std::vector<std::uint8_t> data{};
  };

  /**
   * Manages simulation snapshots in memory for replay scrubbing / rewinding.
   *
   * Holds a permanent base snapshot at beat 0 (game start) and a sliding ring
   * buffer of periodic snapshots (e.g. every 30-60s of simulation).
   */
  class ReplaySnapshotManager
  {
  public:
    explicit ReplaySnapshotManager(std::size_t maxSnapshots = 8);
    ~ReplaySnapshotManager() = default;

    ReplaySnapshotManager(const ReplaySnapshotManager&) = delete;
    ReplaySnapshotManager& operator=(const ReplaySnapshotManager&) = delete;
    ReplaySnapshotManager(ReplaySnapshotManager&&) noexcept = default;
    ReplaySnapshotManager& operator=(ReplaySnapshotManager&&) noexcept = default;

    /**
     * Sets the permanent anchor snapshot at beat 0.
     */
    void SetBaseSnapshot(ReplaySnapshot snapshot);

    /**
     * Returns the permanent anchor snapshot, or nullptr if not yet set.
     */
    [[nodiscard]] const ReplaySnapshot* GetBaseSnapshot() const noexcept;

    /**
     * Adds a periodic snapshot to the sliding ring buffer.
     * If the ring buffer exceeds `mMaxSnapshots`, the oldest snapshot is evicted.
     */
    void AddSnapshot(ReplaySnapshot snapshot);

    /**
     * Finds the nearest snapshot whose beat is <= targetBeat.
     * Checks the ring buffer first, falling back to mBaseSnapshot if none match.
     * Returns nullptr only if no base snapshot and no ring buffer snapshots exist.
     */
    [[nodiscard]] const ReplaySnapshot* FindNearestSnapshot(std::int32_t targetBeat) const noexcept;

    /**
     * Returns the number of periodic snapshots in the ring buffer.
     */
    [[nodiscard]] std::size_t GetSnapshotCount() const noexcept;

    /**
     * Adjusts the maximum capacity of the ring buffer.
     * Evicts oldest snapshots if current size exceeds new capacity.
     */
    void SetMaxSnapshots(std::size_t maxSnapshots) noexcept;

    /**
     * Clears all snapshots (both base snapshot and ring buffer).
     */
    void Clear() noexcept;

  private:
    std::size_t mMaxSnapshots{8};
    std::optional<ReplaySnapshot> mBaseSnapshot{};
    std::deque<ReplaySnapshot> mRingBuffer{};
  };
} // namespace moho
