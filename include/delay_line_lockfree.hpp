#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <optional>
#include <thread>
#include <utility>

// Same idea as DelayLineMutex (delay_line_mutex.hpp), but with no mutex at
// all: a single-producer/single-consumer ring buffer built from std::atomic
// indices with explicit memory ordering, plus C++20 atomic wait/notify so
// the consumer can sleep instead of busy-spinning when the buffer is empty.
//
// Correctness relies on there being exactly one producer thread calling
// push() and exactly one consumer thread calling pop().
template <typename T, std::size_t Capacity>
class DelayLineSPSC {
    static_assert(Capacity >= 2, "Capacity must be at least 2");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");

public:
    explicit DelayLineSPSC(std::chrono::milliseconds delay) : delay_(delay) {}

    // Producer-side only. Returns false if the buffer is currently full.
    bool try_push(T value) {
        std::size_t w = write_.load(std::memory_order_relaxed);
        std::size_t next = (w + 1) & kMask;
        // Acquire: synchronizes with the consumer's release-store of read_,
        // so we see up-to-date "how much space is free" information.
        if (next == read_.load(std::memory_order_acquire)) {
            return false; // full
        }
        slots_[w].value = std::move(value);
        slots_[w].ts = std::chrono::steady_clock::now();
        // Release: publishes slots_[w] to the consumer before it can see
        // the new write_ index.
        write_.store(next, std::memory_order_release);
        write_.notify_one();
        return true;
    }

    // Consumer-side only. Blocks until a frame is available AND its delay
    // has elapsed. Returns std::nullopt once stop() has been called and the
    // buffer has fully drained.
    std::optional<T> pop() {
        std::size_t r = read_.load(std::memory_order_relaxed);
        for (;;) {
            // Acquire: synchronizes with the producer's release-store of
            // write_, so slots_[r] written above is visible here.
            std::size_t w = write_.load(std::memory_order_acquire);
            if (w == r) {
                if (stopped_.load(std::memory_order_relaxed)) return std::nullopt;
                // Sleeps here instead of spinning; wakes on push()'s notify
                // or stop()'s notify. Re-checks the loop condition itself.
                write_.wait(w, std::memory_order_relaxed);
                continue;
            }

            auto ready_at = slots_[r].ts + delay_;
            std::this_thread::sleep_until(ready_at);

            T v = std::move(slots_[r].value);
            std::size_t next = (r + 1) & kMask;
            // Release: tells the producer this slot is free to reuse.
            read_.store(next, std::memory_order_release);
            read_.notify_one();
            r = next;
            return v;
        }
    }

    // Wakes a blocked consumer once the producer is done. pop() still
    // drains whatever remains before returning nullopt.
    void stop() {
        stopped_.store(true, std::memory_order_relaxed);
        write_.notify_all();
    }

private:
    struct Slot {
        T value{};
        std::chrono::steady_clock::time_point ts{};
    };

    static constexpr std::size_t kMask = Capacity - 1;
    std::array<Slot, Capacity> slots_{};

    // Padded to their own cache lines: producer only ever writes write_ and
    // reads read_, consumer only ever writes read_ and reads write_. Without
    // padding, both indices sharing a cache line would cause false sharing
    // (each side's writes would invalidate the other's cache line for no
    // logical reason).
    alignas(64) std::atomic<std::size_t> write_{0};
    alignas(64) std::atomic<std::size_t> read_{0};

    std::chrono::milliseconds delay_;
    std::atomic<bool> stopped_{false};
};
