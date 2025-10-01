#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

using namespace std::chrono_literals;

std::atomic<bool> stop{false};

void worker(int id, std::atomic<int>& ticks) {
    while (!stop.load()) {
        int sum = 0;
        for (int i = 1; i <= 1000; i++) {
            sum += i;
        }
        ++ticks;
        if (id==0 && ticks % 1000 == 0) {
            std::cout << "[worker " << id << "] ticks" <<ticks.load() << std::endl;
        }
        std::this_thread::sleep_for(1ms);
    }
    std::cout << "[worker " << id << "] requested stop, exiting... \n";
}

int main() {
    std::cout << "[main thread (compat)]" << std::endl;

    std::atomic<int> ticks{0};
    const int N =8;

    std::vector<std::thread> threads;
    threads.reserve(N);

    for (int i=0; i < N; ++i) {
        threads.emplace_back(worker, i, std::ref(ticks));
    }

    std::this_thread::sleep_for(2s);

    std::cout << "Requesting stop ..." << std::endl;
    stop.store(true);

    for (auto& t : threads) t.join();

    std::cout << "Total ticks" << ticks.load()<<std::endl;
    std::cout << "Done.\n";
    return 0;

}