// Host tests for WriteQueue (components/subzero_appliance/write_queue.h)
// — the pacing/coalescing/bounding layer every writable entity's BLE
// write goes through. Driven deterministically via FakeScheduler.

#include "../../components/subzero_appliance/write_queue.h"
#include "hub_test_helpers.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace esphome {
namespace subzero_appliance {
namespace {

// Each fired write records (label, fire time) so tests can assert both
// order and pacing.
class WriteQueueTest : public ::testing::Test {
protected:
  void SetUp() override { queue_.set_scheduler(&sched_); }

  // Enqueue a write under `key` that records `label` when it fires.
  WriteQueue::Enqueue enqueue(const std::string &key,
                              const std::string &label) {
    return queue_.enqueue(
        key, [this, label]() { fired_.emplace_back(label, sched_.now_ms()); });
  }

  FakeScheduler sched_;
  WriteQueue queue_;
  std::vector<std::pair<std::string, std::uint32_t>> fired_;
};

TEST_F(WriteQueueTest, FirstWriteFiresImmediately) {
  EXPECT_EQ(enqueue("ref_set_temp", "a"), WriteQueue::Enqueue::kQueued);
  sched_.advance_by(1);
  ASSERT_EQ(fired_.size(), 1u);
  EXPECT_EQ(fired_[0].first, "a");
  EXPECT_EQ(fired_[0].second, 0u);
  EXPECT_EQ(queue_.pending(), 0u);
}

TEST_F(WriteQueueTest, SecondWritePacedBySpacing) {
  enqueue("a_key", "a");
  enqueue("b_key", "b");
  sched_.advance_by(1);
  ASSERT_EQ(fired_.size(), 1u); // only the first fired so far
  sched_.advance_to(WriteQueue::kSpacingMs);
  ASSERT_EQ(fired_.size(), 2u);
  EXPECT_EQ(fired_[1].first, "b");
  EXPECT_EQ(fired_[1].second, WriteQueue::kSpacingMs);
}

TEST_F(WriteQueueTest, DrainsFifoWithSpacing) {
  enqueue("a_key", "a");
  enqueue("b_key", "b");
  enqueue("c_key", "c");
  sched_.advance_to(10 * WriteQueue::kSpacingMs);
  ASSERT_EQ(fired_.size(), 3u);
  EXPECT_EQ(fired_[0].first, "a");
  EXPECT_EQ(fired_[1].first, "b");
  EXPECT_EQ(fired_[2].first, "c");
  EXPECT_EQ(fired_[1].second - fired_[0].second, WriteQueue::kSpacingMs);
  EXPECT_EQ(fired_[2].second - fired_[1].second, WriteQueue::kSpacingMs);
}

// Pacing is enforced against the last write's timestamp, not queue
// occupancy: a write arriving shortly after the previous one fired (into
// an empty queue) must still wait out the remainder of the spacing.
TEST_F(WriteQueueTest, PacesAgainstLastWriteTimestamp) {
  enqueue("a_key", "a");
  sched_.advance_by(1); // "a" fires at t=0
  enqueue("b_key", "b");
  sched_.advance_to(10 * WriteQueue::kSpacingMs);
  ASSERT_EQ(fired_.size(), 2u);
  EXPECT_EQ(fired_[1].second, WriteQueue::kSpacingMs); // not t=1
}

// ...but once the spacing has already elapsed, a fresh write fires
// immediately rather than waiting a full period again.
TEST_F(WriteQueueTest, NoSpuriousDelayAfterQuietPeriod) {
  enqueue("a_key", "a");
  sched_.advance_to(3 * WriteQueue::kSpacingMs); // "a" fired at t=0, long quiet
  enqueue("b_key", "b");
  sched_.advance_by(1);
  ASSERT_EQ(fired_.size(), 2u);
  EXPECT_EQ(fired_[1].second, 3 * WriteQueue::kSpacingMs);
}

TEST_F(WriteQueueTest, CoalescesSameKeyLastValueWins) {
  EXPECT_EQ(enqueue("ref_set_temp", "v1"), WriteQueue::Enqueue::kQueued);
  EXPECT_EQ(enqueue("ref_set_temp", "v2"), WriteQueue::Enqueue::kCoalesced);
  EXPECT_EQ(enqueue("ref_set_temp", "v3"), WriteQueue::Enqueue::kCoalesced);
  EXPECT_EQ(queue_.pending(), 1u); // never grew
  sched_.advance_to(10 * WriteQueue::kSpacingMs);
  ASSERT_EQ(fired_.size(), 1u); // exactly one BLE write
  EXPECT_EQ(fired_[0].first, "v3");
}

// The HA-slider-drag scenario the coalescing exists for: N intermediate
// values while a write is already pending must produce one write with
// the final value, not N spaced writes.
TEST_F(WriteQueueTest, SliderDragSendsOnlyFinalValue) {
  enqueue("frz_set_temp", "-5");
  sched_.advance_by(1); // first write fires immediately at t=0
  for (int v = -6; v >= -10; --v)
    enqueue("frz_set_temp", std::to_string(v));
  sched_.advance_to(10 * WriteQueue::kSpacingMs);
  ASSERT_EQ(fired_.size(), 2u);
  EXPECT_EQ(fired_[1].first, "-10");
  EXPECT_EQ(fired_[1].second, WriteQueue::kSpacingMs);
}

TEST_F(WriteQueueTest, CoalescingKeepsFifoPosition) {
  enqueue("a_key", "a1");
  enqueue("b_key", "b");
  enqueue("a_key", "a2"); // replaces a1 in place, still ahead of b
  sched_.advance_to(10 * WriteQueue::kSpacingMs);
  ASSERT_EQ(fired_.size(), 2u);
  EXPECT_EQ(fired_[0].first, "a2");
  EXPECT_EQ(fired_[1].first, "b");
}

// A grouped select writes several distinct keys; two overlapping
// selections must resolve to the later selection per field without
// growing the queue.
TEST_F(WriteQueueTest, OverlappingGroupedSelectsResolveToLatest) {
  // "Max Ice": ice on, max on, night off
  enqueue("ice_maker_on", "ice=1");
  enqueue("max_ice_on", "max=1");
  enqueue("night_ice_on", "night=0");
  // User immediately picks "Night Ice" instead: ice on, max off, night on
  EXPECT_EQ(enqueue("ice_maker_on", "ice=1'"), WriteQueue::Enqueue::kCoalesced);
  EXPECT_EQ(enqueue("max_ice_on", "max=0"), WriteQueue::Enqueue::kCoalesced);
  EXPECT_EQ(enqueue("night_ice_on", "night=1"),
            WriteQueue::Enqueue::kCoalesced);
  EXPECT_EQ(queue_.pending(), 3u);
  sched_.advance_to(10 * WriteQueue::kSpacingMs);
  ASSERT_EQ(fired_.size(), 3u);
  EXPECT_EQ(fired_[0].first, "ice=1'");
  EXPECT_EQ(fired_[1].first, "max=0");
  EXPECT_EQ(fired_[2].first, "night=1");
}

TEST_F(WriteQueueTest, DropsNewKeysWhenFull) {
  for (std::size_t i = 0; i < WriteQueue::kMaxPending; ++i)
    EXPECT_EQ(enqueue("key" + std::to_string(i), "w"),
              WriteQueue::Enqueue::kQueued);
  EXPECT_EQ(queue_.pending(), WriteQueue::kMaxPending);
  // A genuinely new key is rejected...
  EXPECT_EQ(enqueue("overflow_key", "x"), WriteQueue::Enqueue::kDropped);
  EXPECT_EQ(queue_.pending(), WriteQueue::kMaxPending);
  // ...but coalescing onto an existing key still works at capacity.
  EXPECT_EQ(enqueue("key0", "w'"), WriteQueue::Enqueue::kCoalesced);
  EXPECT_EQ(queue_.pending(), WriteQueue::kMaxPending);
  // Everything queued still drains; the dropped write never fires.
  sched_.advance_to(2 * WriteQueue::kMaxPending * WriteQueue::kSpacingMs);
  EXPECT_EQ(fired_.size(), WriteQueue::kMaxPending);
  for (const auto &f : fired_)
    EXPECT_NE(f.first, "x");
}

TEST_F(WriteQueueTest, QueueUsableAgainAfterDraining) {
  for (std::size_t i = 0; i < WriteQueue::kMaxPending; ++i)
    enqueue("key" + std::to_string(i), "w");
  sched_.advance_to(2 * WriteQueue::kMaxPending * WriteQueue::kSpacingMs);
  EXPECT_EQ(queue_.pending(), 0u);
  EXPECT_EQ(enqueue("post_drain", "y"), WriteQueue::Enqueue::kQueued);
  sched_.advance_by(2 * WriteQueue::kSpacingMs);
  EXPECT_EQ(fired_.back().first, "y");
}

TEST_F(WriteQueueTest, NoSchedulerDropsInsteadOfCrashing) {
  WriteQueue unwired;
  EXPECT_EQ(unwired.enqueue("k", []() {}), WriteQueue::Enqueue::kDropped);
  EXPECT_EQ(unwired.pending(), 0u);
}

} // namespace
} // namespace subzero_appliance
} // namespace esphome
