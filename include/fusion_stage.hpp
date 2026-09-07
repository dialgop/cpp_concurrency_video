#pragma once

#include "delay_line_mutex.hpp"

#include <barrier>
#include <chrono>
#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

struct Frame {
    std::size_t source_id;
    std::uint64_t seq;
    std::chrono::steady_clock::time_point captured_at;
    double value; // stand-in payload, e.g. average pixel luma
};


struct FusedFrame {
    std::chrono::steady_clock::time_point fused_at;
    std::vector<double> source_values;
    std::vector<bool> source_active;
    std::chrono::steady_clock::duration spread;
    double composite;
};

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

    void push(std::size_t source_id, Frame frame) { delay_lines_[source_id]->push(std::move(frame)); }

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
                active_[source_id] = false;
                barrier_.arrive_and_drop();
                return;
            }
            latest_[source_id] = *frame;
            barrier_.arrive_and_wait();
        }
    }

    void do_fuse() {
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
