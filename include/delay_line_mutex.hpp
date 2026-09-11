#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

// Fixed-capacity delay line: a producer pushes frames as they arrive, a
// consumer receives each frame back only once `delay` has elapsed since it
// was pushed. Blocking is done with a mutex + condition_variable.
template <typename T>
class DelayLineMutex {
public:
    DelayLineMutex(std::chrono::milliseconds delay, std::size_t capacity)
        : delay_(delay), capacity_(capacity) {}

    // Producer-side. Blocks while the buffer is full.
    void push(T value) {
        auto ts = std::chrono::steady_clock::now();
        {
            std::unique_lock lock(mutex_);
            cv_not_full_.wait(lock, [&] { return buffer_.size() < capacity_ || stopped_; });
            if (stopped_) return;
            buffer_.push_back(Entry{std::move(value), ts});
        }
        cv_not_empty_.notify_one();
    }

    // Consumer-side. Blocks until the oldest frame's delay has elapsed, then
    // returns it. Returns std::nullopt once stop() has been called and the
    // buffer has drained.
    std::optional<T> pop() {
        std::unique_lock lock(mutex_);
        for (;;) {
            cv_not_empty_.wait(lock, [&] { return !buffer_.empty() || stopped_; });
            if (buffer_.empty()) return std::nullopt;

            auto ready_at = buffer_.front().ts + delay_;
            cv_not_empty_.wait_until(lock, ready_at);
            if (std::chrono::steady_clock::now() < ready_at) continue;

            T v = std::move(buffer_.front().value);
            buffer_.pop_front();
            lock.unlock();
            cv_not_full_.notify_one();
            return v;
        }
    }

    // Non-blocking variant of pop(): returns std::nullopt immediately if the
    // buffer is empty or the oldest frame's delay hasn't elapsed yet, instead
    // of waiting. For a caller that must stay responsive to other work (see
    // PipelineController in Stage 3) rather than dedicating a whole thread
    // to this one queue.
    std::optional<T> try_pop() {
        std::lock_guard lock(mutex_);
        if (buffer_.empty()) return std::nullopt;
        if (std::chrono::steady_clock::now() < buffer_.front().ts + delay_) return std::nullopt;

        T v = std::move(buffer_.front().value);
        buffer_.pop_front();
        cv_not_full_.notify_one();
        return v;
    }

    // Signals producer/consumer to unblock once no more items will be
    // pushed. pop() keeps draining whatever remains before returning nullopt.
    void stop() {
        {
            std::lock_guard lock(mutex_);
            stopped_ = true;
        }
        cv_not_empty_.notify_all();
        cv_not_full_.notify_all();
    }

private:
    struct Entry {
        T value;
        std::chrono::steady_clock::time_point ts;
    };

    std::chrono::milliseconds delay_;
    std::size_t capacity_;
    std::mutex mutex_;
    std::condition_variable cv_not_empty_;
    std::condition_variable cv_not_full_;
    std::deque<Entry> buffer_;
    bool stopped_ = false;
};