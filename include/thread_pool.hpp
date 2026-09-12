#pragma once

#include "command_queue.hpp"

#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

// Stage 4: a small, fixed-size pool of worker threads pulling from one
// shared queue of work - reusing CommandQueue from Stage 3, since a thread
// pool's work queue is exactly the same shape: an unbounded blocking FIFO
// of "things to do." Whichever worker happens to be idle grabs the next
// task, so load balances itself with no explicit scheduling logic, unlike:
//
//   - round-robin: each submitted task is assigned to worker
//     (submission_count % N) up front. Simple, but a run of slow tasks can
//     pile up on one worker while others sit idle, because the assignment
//     never reacts to how busy a worker actually is.
//   - work-stealing: each worker has its own queue and only reaches into
//     another worker's queue when its own is empty. Scales better than one
//     shared queue at high core counts (less contention on a single lock),
//     at the cost of a much more involved implementation - this is what
//     real-world schedulers like Intel TBB and Rust's rayon use.
//
// One shared queue is the simplest design that still load-balances well,
// which is why it's the right starting point.
//
// This also marks a different concurrency shape from Stages 1-3: there, one
// thread was dedicated to one source for its entire lifetime (blocking in a
// loop). Here, many short, independent units of work ("process one frame")
// are farmed out to a small, fixed number of threads - closer to how real
// frame-processing work gets distributed in a broadcast pipeline.
class ThreadPool {
public:
    explicit ThreadPool(std::size_t num_threads) {
        workers_.reserve(num_threads);
        for (std::size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~ThreadPool() {
        // One empty sentinel task per worker: each worker consumes exactly
        // one and exits, so all of them - and only them - get told to stop.
        for (std::size_t i = 0; i < workers_.size(); ++i) {
            tasks_.push(Task{});
        }
        for (auto& w : workers_) w.join();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Submits f(args...) to run on whichever worker is next free, returning
    // a future for its result - reusing the same packaged_task idea from
    // examples/packaged_task.cpp, just handed to a pool instead of a single
    // std::thread.
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using ReturnType = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            [f = std::forward<F>(f), ... captured_args = std::forward<Args>(args)]() mutable {
                return std::invoke(f, captured_args...);
            });
        std::future<ReturnType> future = task->get_future();
        tasks_.push([task] { (*task)(); });
        return future;
    }

private:
    using Task = std::function<void()>;

    void worker_loop() {
        for (;;) {
            Task task = tasks_.pop();
            if (!task) return; // sentinel: this worker's turn to stop
            task();
        }
    }

    CommandQueue<Task> tasks_;
    std::vector<std::thread> workers_;
};