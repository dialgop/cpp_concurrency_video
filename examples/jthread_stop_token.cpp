#include <thread>
#include <stop_token>
#include <chrono>
#include <iostream>
#include <vector>
#include <atomic>

using namespace std::chrono_literals;

void worker(std::stop_token st, int id, std::atomic<int>& ticks) {
    while (!st.stop_requested()) {
        ++ticks;  // simulated work
        if (id == 0 && ticks % 1000 == 0) {
            std::cout << "[worker " << id << "] ticks=" << ticks.load() << "\n";
        }
        std::this_thread::sleep_for(1ms);
    }
    std::cout << "[worker " << id << "] stop requested , exiting...\n";
}

int main() {
    std::cout << "Hello jthreads!\n";

    std::atomic<int> ticks{0};
    const int N = 4;

    std::vector<std::jthread> threads;
    threads.reserve(N);

    for (int i = 0; i < N; ++i) {
        // std::jthread automatically passes the stop_token to the worker
        threads.emplace_back(worker, i, std::ref(ticks));
    }

    std::this_thread::sleep_for(2s);

    std::cout << "Requesting stop...\n";
    for (auto& jt : threads) {
        jt.request_stop(); // all threads finish
    }

    std::cout << "Total ticks = " << ticks.load() << "\n";
    std::cout << "Done.\n";
    return 0;
}
