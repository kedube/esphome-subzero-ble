#pragma once

// Serialized, paced queue for BLE property writes — the write-side
// counterpart of the Scheduler test seam.
//
// Live testing showed that issuing several BLE writes in the same loop
// iteration — whether from one grouped-select mode change or from two
// unrelated entities toggled close together — could overwhelm the stack
// and drop or corrupt one of them. Every writable entity's write goes
// through this single queue so the pacing protection isn't limited to
// grouped selects.
//
// Behavior:
//   * FIFO, one write per kSpacingMs. Pacing is enforced against the
//     last write's timestamp (not just "is the queue non-empty"), so two
//     writes arriving a few ms apart but each finding an empty queue —
//     e.g. two unrelated entities toggled close together — still get
//     spaced out instead of firing back-to-back. The first write after
//     boot fires immediately.
//   * Same-key coalescing: a write to a key that already has a pending
//     write replaces that entry in place (keeping its FIFO position)
//     instead of appending. Dragging an HA number slider fires every
//     intermediate value; without coalescing a 7-step drag would march
//     the physical setpoint through 7 writes over ~5 seconds. With it,
//     the last value wins. Grouped selects are unaffected (their fields
//     are distinct keys), and two overlapping mode selections resolve to
//     the later selection per field, which is the intended outcome.
//   * Bounded: at most kMaxPending distinct pending writes. Coalescing
//     never grows the queue, so it still works when full; a genuinely
//     new key is rejected with kDropped (the caller logs it). This is a
//     backstop against a runaway HA automation hammering writable
//     entities — without it the deque grows without limit while
//     draining at one write per 750ms, and stale writes keep moving
//     physical setpoints long after the trigger stopped.
//
// Depends only on the Scheduler interface + std, so host tests drive it
// deterministically with FakeScheduler (see write_queue_test.cpp).

#include "scheduler.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>

namespace esphome {
namespace subzero_appliance {

class WriteQueue {
public:
  // See enqueue().
  enum class Enqueue { kQueued, kCoalesced, kDropped };

  static constexpr std::uint32_t kSpacingMs = 750;
  static constexpr std::size_t kMaxPending = 16;

  // Must be called before the first enqueue (production wires this in
  // ApplianceBase::setup(), which runs before HA can touch any entity).
  void set_scheduler(Scheduler *s) { scheduler_ = s; }

  // Queue `write_fn` (which performs the actual BLE write) under `key`
  // (the appliance property name it writes). Returns:
  //   kQueued    — appended; will fire in FIFO order, paced.
  //   kCoalesced — replaced an existing pending write to the same key.
  //   kDropped   — queue full with no same-key entry (or no scheduler);
  //                the write was discarded and the caller should log it.
  Enqueue enqueue(const std::string &key, std::function<void()> write_fn) {
    if (scheduler_ == nullptr)
      return Enqueue::kDropped;
    for (auto &pending : queue_) {
      if (pending.key == key) {
        pending.fn = std::move(write_fn);
        return Enqueue::kCoalesced;
      }
    }
    if (queue_.size() >= kMaxPending)
      return Enqueue::kDropped;
    queue_.push_back(PendingWrite{key, std::move(write_fn)});
    schedule_drain_();
    return Enqueue::kQueued;
  }

  std::size_t pending() const { return queue_.size(); }

  // Discard all pending writes and return how many were dropped. Called
  // on disconnect: queued writes can be up to kMaxPending * kSpacingMs
  // old, and letting them fire into the *next* session before its channel
  // unlock completes both fails the writes and risks interleaving with
  // the subscribe ladder.
  std::size_t clear() {
    const std::size_t dropped = queue_.size();
    queue_.clear();
    return dropped;
  }

private:
  struct PendingWrite {
    std::string key;
    std::function<void()> fn;
  };

  // Schedules drain_() for whenever kSpacingMs has elapsed since the
  // last write (immediately, if it already has, or if this is the first
  // write since boot). No-ops if a drain is already scheduled or the
  // queue is empty, so this is safe to call after every enqueue and
  // after every drain. Unsigned subtraction keeps `elapsed` correct
  // across millis() rollover.
  void schedule_drain_() {
    if (drain_scheduled_ || queue_.empty())
      return;
    const std::uint32_t now = scheduler_->now_ms();
    const std::uint32_t elapsed = now - last_write_ms_;
    const std::uint32_t delay = (have_written_ && elapsed < kSpacingMs)
                                    ? (kSpacingMs - elapsed)
                                    : 0;
    drain_scheduled_ = true;
    scheduler_->set_timeout("subzero_write_queue", delay,
                            [this]() { this->drain_(); });
  }

  void drain_() {
    drain_scheduled_ = false;
    if (queue_.empty())
      return;
    auto write_fn = std::move(queue_.front().fn);
    queue_.pop_front();
    last_write_ms_ = scheduler_->now_ms();
    have_written_ = true;
    write_fn();
    schedule_drain_();
  }

  Scheduler *scheduler_ = nullptr;
  std::deque<PendingWrite> queue_;
  std::uint32_t last_write_ms_ = 0;
  bool drain_scheduled_ = false;
  bool have_written_ = false;
};

} // namespace subzero_appliance
} // namespace esphome
