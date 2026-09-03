#include <thread>
#include <iostream>
#include <vector>
#include <mutex>

int counter = 0;
std::mutex mtx;

void increment_safe() {
    for (int i=0; i <100000; ++i) {
        std::lock_guard<std::mutex> loc(mtx);
        ++counter;
    }
}

int main() {
    std::cout << "===mutex demo ===" << std::endl;
    std::thread t1(increment_safe);
    std::thread t2(increment_safe);

    t1.join();
    t2.join();

    std::cout << "Final result " << counter << std::endl;
}