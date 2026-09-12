// Stage 4: thread pool.
//
// Benchmarks total wall-clock time to process the same fixed batch of
// simulated frame-processing work (include/thread_pool.hpp) across
// increasing pool sizes, to show throughput scaling with thread count.

#include "thread_pool.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

constexpr int kNumFeeds = 6;
constexpr int kFramesPerFeed = 40;
constexpr int kWorkIterations = 20000; // calibrated to make one task take roughly ~1ms

// Stands in for "processing a video frame": CPU-bound, no sleeping/I-O, so
// the benchmark measures real parallel speedup rather than being dominated
// by threads just waiting around.
double process_frame(int feed_id, int frame_id) {
    double acc = static_cast<double>(feed_id * 1000 + frame_id);
    for (int i = 0; i < kWorkIterations; ++i) {
        acc = std::sin(acc) * std::cos(acc) + static_cast<double>(i % 7);
    }
    return acc;
}

double run_with_pool_size(std::size_t num_threads) {
    ThreadPool pool(num_threads);
    std::vector<std::future<double>> futures;
    futures.reserve(kNumFeeds * kFramesPerFeed);

    auto start = std::chrono::steady_clock::now();
    for (int feed = 0; feed < kNumFeeds; ++feed) {
        for (int frame = 0; frame < kFramesPerFeed; ++frame) {
            futures.push_back(pool.submit(process_frame, feed, frame));
        }
    }
    double sum = 0.0;
    for (auto& f : futures) sum += f.get();
    auto elapsed = std::chrono::steady_clock::now() - start;
    (void)sum; // forces the futures to actually be awaited; value itself is unused
    return std::chrono::duration<double, std::milli>(elapsed).count();
}

} // namespace

int main() {
    unsigned hw = std::thread::hardware_concurrency();
    std::cout << "Stage 4: thread pool benchmark\n";
    std::cout << "hardware_concurrency() = " << hw << "\n";
    std::cout << kNumFeeds << " feeds x " << kFramesPerFeed << " frames = " << (kNumFeeds * kFramesPerFeed)
               << " tasks per run\n\n";

    std::vector<std::size_t> pool_sizes = {1, 2, 4};
    if (hw > 0 && std::find(pool_sizes.begin(), pool_sizes.end(), hw) == pool_sizes.end()) {
        pool_sizes.push_back(hw);
    }
    std::sort(pool_sizes.begin(), pool_sizes.end());

    double baseline_ms = 0.0;
    std::cout << std::fixed << std::setprecision(1);
    for (std::size_t n : pool_sizes) {
        double ms = run_with_pool_size(n);
        if (n == 1) baseline_ms = ms;
        double speedup = baseline_ms > 0.0 ? baseline_ms / ms : 1.0;
        std::cout << "  threads=" << std::setw(2) << n << "  total=" << std::setw(7) << ms << " ms  speedup="
                   << std::setw(4) << speedup << "x\n";
    }

    std::cout << "\nDone.\n";
}
