// Stage 1: delay-line ring buffer.
//
// Simulates a broadcast delay line: a "capture" thread produces frames as
// fast as a camera would (~30fps), and a "playout" thread must not emit
// each frame until a fixed delay has elapsed since it was captured. Two
// implementations of the same idea are run back to back so their measured
// latency can be compared:
//   1. DelayLineMutex   - mutex + condition_variable (include/delay_line_mutex.hpp)
//   2. DelayLineSPSC    - lock-free, std::atomic only (include/delay_line_lockfree.hpp)

#include "delay_line_mutex.hpp"
#include "delay_line_lockfree.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

struct Frame {
    int id;
    std::chrono::steady_clock::time_point captured_at;
};

constexpr int kFrameCount = 30;
constexpr auto kFrameInterval = 33ms; // ~30 fps
constexpr auto kDelay = 200ms;

void print_summary(const std::vector<double>& latencies_ms) {
    if (latencies_ms.empty()) return;
    double sum = std::accumulate(latencies_ms.begin(), latencies_ms.end(), 0.0);
    double avg = sum / static_cast<double>(latencies_ms.size());
    auto [min_it, max_it] = std::minmax_element(latencies_ms.begin(), latencies_ms.end());
    std::cout << std::fixed << std::setprecision(1)
              << "  -> target delay " << kDelay.count() << " ms | measured avg "
              << avg << " ms (min " << *min_it << ", max " << *max_it << ")\n";
}

void run_mutex_demo() {
    std::cout << "\n=== DelayLineMutex (mutex + condition_variable) ===\n";
    DelayLineMutex<Frame> line(kDelay, /*capacity=*/8);

    std::thread producer([&] {
        for (int i = 0; i < kFrameCount; ++i) {
            line.push(Frame{i, std::chrono::steady_clock::now()});
            std::this_thread::sleep_for(kFrameInterval);
        }
        line.stop();
    });

    std::vector<double> latencies_ms;
    while (auto frame = line.pop()) {
        auto now = std::chrono::steady_clock::now();
        double latency = std::chrono::duration<double, std::milli>(now - frame->captured_at).count();
        latencies_ms.push_back(latency);
    }
    producer.join();

    std::cout << "  released " << latencies_ms.size() << "/" << kFrameCount << " frames\n";
    print_summary(latencies_ms);
}

void run_lockfree_demo() {
    std::cout << "\n=== DelayLineSPSC (lock-free, std::atomic) ===\n";
    DelayLineSPSC<Frame, 8> line(kDelay);

    std::thread producer([&] {
        for (int i = 0; i < kFrameCount; ++i) {
            while (!line.try_push(Frame{i, std::chrono::steady_clock::now()})) {
                std::this_thread::sleep_for(1ms); // buffer full, back off briefly
            }
            std::this_thread::sleep_for(kFrameInterval);
        }
        line.stop();
    });

    std::vector<double> latencies_ms;
    while (auto frame = line.pop()) {
        auto now = std::chrono::steady_clock::now();
        double latency = std::chrono::duration<double, std::milli>(now - frame->captured_at).count();
        latencies_ms.push_back(latency);
    }
    producer.join();

    std::cout << "  released " << latencies_ms.size() << "/" << kFrameCount << " frames\n";
    print_summary(latencies_ms);
}

} // namespace

int main() {
    std::cout << "Stage 1: delay-line ring buffer demo\n";
    run_mutex_demo();
    run_lockfree_demo();
    std::cout << "\nDone.\n";
}
