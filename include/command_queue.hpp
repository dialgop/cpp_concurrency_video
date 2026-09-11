#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>

// A plain unbounded blocking FIFO for messages. Deliberately simpler than
// Stage 1's DelayLineMutex: control-plane traffic (a handful of commands)
// doesn't need the capacity bound or delayed-release logic that mattered
// for a real-time frame stream, just "wait for the next message, in order."
template <typename T>
class CommandQueue {
public:
    void push(T value) {
        {
            std::lock_guard lock(mutex_);
            queue_.push_back(std::move(value));
        }
        cv_.notify_one();
    }

    // Blocks until a message is available, then returns it.
    T pop() {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [&] { return !queue_.empty(); });
        T v = std::move(queue_.front());
        queue_.pop_front();
        return v;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<T> queue_;
};