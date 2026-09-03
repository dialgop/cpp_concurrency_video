#include <iostream>
#include <future>
#include <thread>
#include <chrono>

// Part 1: Factorial using future and async

int factorial(int n) {
    std::cout << "[worker] calculating factorial (" << n << ")" << std::endl;
    int result = 1;
    for (int i=1; i <=n; ++i) {
        result *= i;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    std::cout << "[worker] finishing factorial (" << n << ") = " << result << std::endl;
    return result;
}

int main() {
    std::cout << " === Async demo ===" << std::endl;

    //Throws the function in an asynchronous thread
    std::future<int> fut = std::async(std::launch::async, factorial, 5);

    std::cout << "[main] doing other things meanwhile" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    std::cout << "[Main] still waiting for the result" << std::endl;

    int value = fut.get();
    std::cout << " [Main] received result " << value << std::endl;
}

