#include "moho/sim/ReplaySnapshotManager.h"

namespace moho
{
  std::int32_t sim_ReplaySnapshotInterval = 300;
  std::int32_t sim_DumpSnapshots = 0;

  ReplaySnapshotManager::ReplaySnapshotManager(const std::size_t maxSnapshots)
    : mMaxSnapshots(maxSnapshots > 0 ? maxSnapshots : 1)
  {
  }

  void ReplaySnapshotManager::SetBaseSnapshot(ReplaySnapshot snapshot)
  {
    std::lock_guard<std::mutex> lock(mMutex);
    mBaseSnapshot = std::move(snapshot);
  }

  std::optional<ReplaySnapshot> ReplaySnapshotManager::GetBaseSnapshot() const
  {
    std::lock_guard<std::mutex> lock(mMutex);
    return mBaseSnapshot;
  }

  bool ReplaySnapshotManager::HasBaseSnapshot() const
  {
    std::lock_guard<std::mutex> lock(mMutex);
    return mBaseSnapshot.has_value();
  }

  void ReplaySnapshotManager::AddSnapshot(ReplaySnapshot snapshot)
  {
    std::lock_guard<std::mutex> lock(mMutex);
    while (mRingBuffer.size() >= mMaxSnapshots) {
      mRingBuffer.pop_front();
    }
    mRingBuffer.push_back(std::move(snapshot));
  }

  std::optional<ReplaySnapshot> ReplaySnapshotManager::FindNearestSnapshot(const std::int32_t targetBeat) const
  {
    std::lock_guard<std::mutex> lock(mMutex);

    // Check ring buffer from newest to oldest for the largest beat <= targetBeat
    for (auto it = mRingBuffer.rbegin(); it != mRingBuffer.rend(); ++it) {
      if (it->beat <= targetBeat) {
        return *it;
      }
    }

    // Fall back to base snapshot if valid and <= targetBeat
    if (mBaseSnapshot.has_value() && mBaseSnapshot->beat <= targetBeat) {
      return mBaseSnapshot;
    }

    return std::nullopt;
  }

  std::size_t ReplaySnapshotManager::GetSnapshotCount() const
  {
    std::lock_guard<std::mutex> lock(mMutex);
    return mRingBuffer.size();
  }

  void ReplaySnapshotManager::SetMaxSnapshots(const std::size_t maxSnapshots)
  {
    std::lock_guard<std::mutex> lock(mMutex);
    mMaxSnapshots = maxSnapshots > 0 ? maxSnapshots : 1;
    while (mRingBuffer.size() > mMaxSnapshots) {
      mRingBuffer.pop_front();
    }
  }

  void ReplaySnapshotManager::Clear()
  {
    std::lock_guard<std::mutex> lock(mMutex);
    mBaseSnapshot.reset();
    mRingBuffer.clear();
  }
  bool LoadSnapshotFromFile(const char* filepath, ReplaySnapshot& outSnapshot)
  {
    if (!filepath || filepath[0] == '\0') {
      return false;
    }
    std::FILE* const fp = std::fopen(filepath, "rb");
    if (!fp) {
      return false;
    }
    boost::shared_ptr<std::FILE> sharedFp(fp, &std::fclose);
    SReplaySnapshotFileHeader hdr{};
    if (std::fread(&hdr, 1, sizeof(hdr), fp) == sizeof(hdr) &&
        std::memcmp(hdr.magic, "FAFSNAP1", 8) == 0) {
      outSnapshot.beat = hdr.beat;
      outSnapshot.streamOffset = hdr.streamOffset;
      outSnapshot.lastPeriodicBeat = hdr.lastPeriodicBeat;
      outSnapshot.simHash = hdr.simHash;
      std::memcpy(outSnapshot.simHashes, hdr.simHashes, sizeof(outSnapshot.simHashes));
      outSnapshot.md5Context = hdr.md5Context;
      outSnapshot.filePayloadOffset = hdr.archivePayloadOffset;
      outSnapshot.file = sharedFp;
      return true;
    }
    std::fseek(fp, 0, SEEK_SET);
    outSnapshot.beat = 0;
    outSnapshot.streamOffset = 0;
    outSnapshot.lastPeriodicBeat = 0;
    outSnapshot.md5Context.Reset();
    outSnapshot.filePayloadOffset = 0;
    outSnapshot.file = sharedFp;
    return true;
  }
} // namespace moho
