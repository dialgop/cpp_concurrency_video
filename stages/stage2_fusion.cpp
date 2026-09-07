#include "fusion_stage.hpp"

#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <latch>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

constexpr std::size_t kNumSources = 3;
constexpr const char* kSourceNames[kNumSources] = {"camera", "key", "fill"};
constexpr int kFrameCount = 30;
constexpr auto kFrameInterval = 33ms; // ~30 fps
constexpr auto kPerSourceDelay = 150ms;

double source_value(std::size_t source_id, int frame_id) {
    return static_cast<double>(source_id) * 100.0 + static_cast<double>(frame_id);
}

} // namespace

int main() {
    std::cout << "Stage 2: multi-source fusion with std::latch + std::barrier\n\n";

    std::vector<FusedFrame> fused_frames;
    std::mutex fused_frames_mutex;

    FusionStage fusion(kNumSources, kPerSourceDelay, [&](FusedFrame f) {
        std::lock_guard lock(fused_frames_mutex);
        fused_frames.push_back(std::move(f));
    });
    fusion.start_workers();

    std::latch start_gate(kNumSources);

    std::vector<std::thread> producers;
    producers.reserve(kNumSources);
    for (std::size_t source_id = 0; source_id < kNumSources; ++source_id) {
        producers.emplace_back([&, source_id] {
            start_gate.arrive_and_wait();
            for (int i = 0; i < kFrameCount; ++i) {
                Frame frame{source_id, static_cast<std::uint64_t>(i), std::chrono::steady_clock::now(),
                            source_value(source_id, i)};
                fusion.push(source_id, frame);
                std::this_thread::sleep_for(kFrameInterval);
            }
            fusion.stop_source(source_id);
        });
    }

    for (auto& t : producers) t.join();
    fusion.join_workers();

    std::cout << std::fixed << std::setprecision(1);
    for (std::size_t j = 0; j < fused_frames.size(); ++j) {
        const auto& f = fused_frames[j];
        double spread_us = std::chrono::duration<double, std::micro>(f.spread).count();
        std::cout << "  fused #" << std::setw(2) << j << " composite=" << std::setw(6) << f.composite
                   << "  spread=" << std::setw(6) << spread_us << " us  (";
        for (std::size_t s = 0; s < kNumSources; ++s) {
            if (!f.source_active[s]) continue;
            std::cout << kSourceNames[s] << "=" << f.source_values[s] << " ";
        }
        std::cout << ")\n";
    }

    double avg_spread_us = 0.0;
    if (!fused_frames.empty()) {
        double total = 0.0;
        for (const auto& f : fused_frames) total += std::chrono::duration<double, std::micro>(f.spread).count();
        avg_spread_us = total / static_cast<double>(fused_frames.size());
    }
    std::cout << "\n  fused " << fused_frames.size() << " frames, avg cross-source spread " << avg_spread_us
              << " us\n";
    std::cout << "\nDone.\n";
}
