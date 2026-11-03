#pragma once

#include "boost/shared_ptr.h"
#include "moho/misc/CSaveGameRequestImpl.h"
#include "gpg/core/algorithms/MD5.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

namespace moho
{
  extern std::int32_t sim_ReplaySnapshotInterval;
  extern std::int32_t sim_DumpSnapshots;

  /**
   * Represents an in-memory simulation snapshot taken during replay playback.
   */
  struct ReplaySnapshot
  {
    std::int32_t beat{0};
    std::uint64_t streamOffset{0};
    std::int32_t lastPeriodicBeat{0};
    gpg::MD5Digest simHash{};
    gpg::MD5Digest simHashes[128]{};
    gpg::MD5Context md5Context{};
    boost::shared_ptr<std::FILE> file{};
    std::vector<std::uint8_t> data{};
    std::uint64_t filePayloadOffset{0};
  };

#pragma pack(push, 1)
  /**
   * On-disk header for exported/dumped simulation snapshots (.snap).
   */
  struct SReplaySnapshotFileHeader
  {
    char magic[8]{'F', 'A', 'F', 'S', 'N', 'A', 'P', '1'};
    std::uint32_t headerSize{sizeof(SReplaySnapshotFileHeader)};
    std::int32_t beat{0};
    std::uint64_t streamOffset{0};
    std::int32_t lastPeriodicBeat{0};
    gpg::MD5Digest simHash{};
    gpg::MD5Digest simHashes[128]{};
    gpg::MD5Context md5Context{};
    std::uint64_t archivePayloadOffset{sizeof(SReplaySnapshotFileHeader)};
    std::uint64_t archivePayloadSize{0};
  };
#pragma pack(pop)

  bool LoadSnapshotFromFile(const char* filepath, ReplaySnapshot& outSnapshot);

  /**
   * Manages simulation snapshots in memory for replay scrubbing / rewinding.
   *
   * Holds a permanent base snapshot at beat 0 (game start) and a sliding ring
   * buffer of periodic snapshots (e.g. every 30-60s of simulation).
   * Thread-safe: methods are synchronized via an internal mutex.
   */
  class ReplaySnapshotManager
  {
  public:
    explicit ReplaySnapshotManager(std::size_t maxSnapshots = 8);
    ~ReplaySnapshotManager() = default;

    ReplaySnapshotManager(const ReplaySnapshotManager&) = delete;
    ReplaySnapshotManager& operator=(const ReplaySnapshotManager&) = delete;
    ReplaySnapshotManager(ReplaySnapshotManager&&) = delete;
    ReplaySnapshotManager& operator=(ReplaySnapshotManager&&) = delete;

    /**
     * Sets the permanent anchor snapshot at beat 0.
     */
    void SetBaseSnapshot(ReplaySnapshot snapshot);

    /**
     * Returns the permanent anchor snapshot, or std::nullopt if not yet set.
     */
    [[nodiscard]] std::optional<ReplaySnapshot> GetBaseSnapshot() const;

    /**
     * Returns true if base snapshot has been set.
     */
    [[nodiscard]] bool HasBaseSnapshot() const;

    /**
     * Adds a periodic snapshot to the sliding ring buffer.
     * If the ring buffer exceeds `mMaxSnapshots`, the oldest snapshot is evicted.
     */
    void AddSnapshot(ReplaySnapshot snapshot);

    /**
     * Finds the nearest snapshot whose beat is <= targetBeat.
     * Checks the ring buffer first, falling back to mBaseSnapshot if none match.
     * Returns std::nullopt only if no base snapshot and no ring buffer snapshots exist.
     */
    [[nodiscard]] std::optional<ReplaySnapshot> FindNearestSnapshot(std::int32_t targetBeat) const;

    /**
     * Returns the number of periodic snapshots in the ring buffer.
     */
    [[nodiscard]] std::size_t GetSnapshotCount() const;

    /**
     * Adjusts the maximum capacity of the ring buffer.
     * Evicts oldest snapshots if current size exceeds new capacity.
     */
    void SetMaxSnapshots(std::size_t maxSnapshots);

    /**
     * Clears all snapshots (both base snapshot and ring buffer).
     */
    void Clear();

  private:
    mutable std::mutex mMutex{};
    std::size_t mMaxSnapshots{8};
    std::optional<ReplaySnapshot> mBaseSnapshot{};
    std::deque<ReplaySnapshot> mRingBuffer{};
  };
} // namespace moho
