#include "moho/sim/ReplaySnapshotManager.h"

namespace moho
{
  ReplaySnapshotManager::ReplaySnapshotManager(const std::size_t maxSnapshots)
    : mMaxSnapshots(maxSnapshots > 0 ? maxSnapshots : 1)
  {
  }

  void ReplaySnapshotManager::SetBaseSnapshot(ReplaySnapshot snapshot)
  {
    mBaseSnapshot = std::move(snapshot);
  }

  const ReplaySnapshot* ReplaySnapshotManager::GetBaseSnapshot() const noexcept
  {
    return mBaseSnapshot.has_value() ? &mBaseSnapshot.value() : nullptr;
  }

  void ReplaySnapshotManager::AddSnapshot(ReplaySnapshot snapshot)
  {
    while (mRingBuffer.size() >= mMaxSnapshots) {
      mRingBuffer.pop_front();
    }
    mRingBuffer.push_back(std::move(snapshot));
  }

  const ReplaySnapshot* ReplaySnapshotManager::FindNearestSnapshot(const std::int32_t targetBeat) const noexcept
  {
    const ReplaySnapshot* bestMatch = nullptr;

    // Check ring buffer from newest to oldest for the largest beat <= targetBeat
    for (auto it = mRingBuffer.rbegin(); it != mRingBuffer.rend(); ++it) {
      if (it->beat <= targetBeat) {
        bestMatch = &(*it);
        break;
      }
    }

    if (bestMatch != nullptr) {
      return bestMatch;
    }

    // Fall back to base snapshot if valid and <= targetBeat
    if (mBaseSnapshot.has_value() && mBaseSnapshot->beat <= targetBeat) {
      return &mBaseSnapshot.value();
    }

    return nullptr;
  }

  std::size_t ReplaySnapshotManager::GetSnapshotCount() const noexcept
  {
    return mRingBuffer.size();
  }

  void ReplaySnapshotManager::SetMaxSnapshots(const std::size_t maxSnapshots) noexcept
  {
    mMaxSnapshots = maxSnapshots > 0 ? maxSnapshots : 1;
    while (mRingBuffer.size() > mMaxSnapshots) {
      mRingBuffer.pop_front();
    }
  }

  void ReplaySnapshotManager::Clear() noexcept
  {
    mBaseSnapshot.reset();
    mRingBuffer.clear();
  }
} // namespace moho
