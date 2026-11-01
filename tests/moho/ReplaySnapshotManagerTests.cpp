#include "moho/sim/ReplaySnapshotManager.h"
#include <cassert>
#include <cstdio>

int main()
{
  moho::ReplaySnapshotManager manager(3);
  assert(manager.GetSnapshotCount() == 0);
  assert(manager.FindNearestSnapshot(100) == nullptr);

  // Set base snapshot at beat 0
  moho::ReplaySnapshot baseSnap;
  baseSnap.beat = 0;
  baseSnap.streamOffset = 1000;
  manager.SetBaseSnapshot(baseSnap);
  assert(manager.GetBaseSnapshot() != nullptr);
  assert(manager.GetBaseSnapshot()->beat == 0);

  // When ring buffer is empty, FindNearestSnapshot returns base snapshot
  const auto* snap = manager.FindNearestSnapshot(150);
  assert(snap != nullptr && snap->beat == 0);

  // Add periodic snapshots
  moho::ReplaySnapshot s1;
  s1.beat = 300;
  s1.streamOffset = 2000;
  moho::ReplaySnapshot s2;
  s2.beat = 600;
  s2.streamOffset = 3000;
  moho::ReplaySnapshot s3;
  s3.beat = 900;
  s3.streamOffset = 4000;
  manager.AddSnapshot(s1);
  manager.AddSnapshot(s2);
  manager.AddSnapshot(s3);
  assert(manager.GetSnapshotCount() == 3);

  // Lookup tests
  assert(manager.FindNearestSnapshot(200)->beat == 0);
  assert(manager.FindNearestSnapshot(300)->beat == 300);
  assert(manager.FindNearestSnapshot(599)->beat == 300);
  assert(manager.FindNearestSnapshot(600)->beat == 600);
  assert(manager.FindNearestSnapshot(1000)->beat == 900);

  // Eviction test: adding 4th snapshot evicts s1 (beat 300)
  moho::ReplaySnapshot s4;
  s4.beat = 1200;
  s4.streamOffset = 5000;
  manager.AddSnapshot(s4);
  assert(manager.GetSnapshotCount() == 3);
  assert(manager.FindNearestSnapshot(400)->beat == 0); // s1 was evicted, falls back to base

  // Clear test
  manager.Clear();
  assert(manager.GetSnapshotCount() == 0);
  assert(manager.GetBaseSnapshot() == nullptr);

  std::printf("ReplaySnapshotManager tests passed!\n");
  return 0;
}
