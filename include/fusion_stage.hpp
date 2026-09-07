#pragma once

#include "delay_line_mutex.hpp"

#include <barrier>
#include <chrono>
#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

// A frame captured by one of several sources (e.g. camera / key / fill in a
// broadcast keyer), timestamped so a FusionStage can tell how far apart two
// sources' contributions to a fused frame really were.
struct Frame {
    std::size_t source_id;
    std::uint64_t seq;
    std::chrono::steady_clock::time_point captured_at;
    double value; // stand-in payload, e.g. average pixel luma
};

// The result of combining one frame from every still-active source.
struct FusedFrame {
    std::chrono::steady_clock::time_point fused_at;
    std::vector<double> source_values;         // indexed by source_id, only for active sources
    std::vector<bool> source_active;            // which indices were actually contributed this phase
    std::chrono::steady_clock::duration spread; // max - min captured_at among contributing sources
    double composite;                           // average of contributing source values
};

// Stage 2: several capture threads, one per source, must be brought to the
// same point in time before their frames are combined. Where Stage 1 solved
// "release this one frame after a delay," Stage 2 solves a different
// problem: "wait for a *group* of independent threads to all be ready, then
// run the combining step exactly once." That's what std::barrier is for -
// a queue can't express "don't proceed until everyone else has arrived."
//
// Each source gets its own Stage 1 delay line as an input buffer (so late
// jitter on one source doesn't corrupt another's timing), and one worker
// thread per source pops from it and rendezvous at a shared std::barrier.
// The barrier's completion function - invoked exactly once per phase, by
// whichever thread happens to be the last to arrive - does the fusion.
//
// Because every worker's writes to latest_[source_id] happen-before it
// calls arrive_and_wait(), and std::barrier guarantees those happen-before
// the completion function runs, do_fuse() can read latest_ with no extra
// locking: the barrier itself is the synchronization.
class FusionStage {
public:
    using FusedCallback = std::function<void(FusedFrame)>;

    FusionStage(std::size_t num_sources, std::chrono::milliseconds per_source_delay, FusedCallback on_fused)
        : num_sources_(num_sources),
          latest_(num_sources),
          active_(num_sources, true),
          on_fused_(std::move(on_fused)),
          barrier_(static_cast<std::ptrdiff_t>(num_sources), [this] { do_fuse(); }) {
        delay_lines_.reserve(num_sources);
        for (std::size_t i = 0; i < num_sources; ++i) {
            delay_lines_.push_back(std::make_unique<DelayLineMutex<Frame>>(per_source_delay, /*capacity=*/8));
        }
    }

    // Capture-side: call from source i's own capture thread.
    void push(std::size_t source_id, Frame frame) { delay_lines_[source_id]->push(std::move(frame)); }

    // Call once a source's capture thread has produced its last frame.
    void stop_source(std::size_t source_id) { delay_lines_[source_id]->stop(); }

    void start_workers() {
        workers_.reserve(num_sources_);
        for (std::size_t i = 0; i < num_sources_; ++i) {
            workers_.emplace_back([this, i] { worker_loop(i); });
        }
    }

    void join_workers() {
        for (auto& t : workers_) t.join();
    }

private:
    void worker_loop(std::size_t source_id) {
        for (;;) {
            auto frame = delay_lines_[source_id]->pop();
            if (!frame) {
                // No more data will ever arrive from this source. Dropping
                // (rather than just returning) tells the barrier to expect
                // one fewer arrival on every future phase, so the remaining
                // sources are never left waiting on a thread that's gone.
                active_[source_id] = false;
                barrier_.arrive_and_drop();
                return;
            }
            latest_[source_id] = *frame;
            barrier_.arrive_and_wait();
        }
    }

    void do_fuse() {
        // arrive_and_drop() counts as an arrival, so the phase in which the
        // last active source(s) drop out still completes this phase and
        // runs do_fuse() once more - with nothing left active. That final
        // call carries no real data, so it's discarded here rather than
        // handed to on_fused_.
        bool any_active = false;
        for (std::size_t i = 0; i < num_sources_; ++i) any_active = any_active || active_[i];
        if (!any_active) return;

        FusedFrame fused;
        fused.fused_at = std::chrono::steady_clock::now();
        fused.source_values.assign(num_sources_, 0.0);
        fused.source_active = active_;

        auto min_ts = std::chrono::steady_clock::time_point::max();
        auto max_ts = std::chrono::steady_clock::time_point::min();
        double sum = 0.0;
        std::size_t count = 0;
        for (std::size_t i = 0; i < num_sources_; ++i) {
            if (!active_[i]) continue;
            fused.source_values[i] = latest_[i].value;
            sum += latest_[i].value;
            ++count;
            min_ts = std::min(min_ts, latest_[i].captured_at);
            max_ts = std::max(max_ts, latest_[i].captured_at);
        }
        fused.composite = count > 0 ? sum / static_cast<double>(count) : 0.0;
        fused.spread = count > 0 ? (max_ts - min_ts) : std::chrono::steady_clock::duration::zero();

        if (on_fused_) on_fused_(std::move(fused));
    }

    std::size_t num_sources_;
    std::vector<std::unique_ptr<DelayLineMutex<Frame>>> delay_lines_;
    std::vector<Frame> latest_;
    std::vector<bool> active_;
    FusedCallback on_fused_;
    std::barrier<std::function<void()>> barrier_;
    std::vector<std::thread> workers_;
};
