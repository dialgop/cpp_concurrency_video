#include <iostream>
#include <thread>
#include <future>

int factorial(int n) {
    int res = 1;
    for (int i= n; i > 1; --i) {
        res *= i;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    return res;
}

int sum(int a, int b) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return a + b;
}

int main() {
    std::packaged_task<int(int)> task(factorial);

    std::future<int> fut = task.get_future();

    std::thread t(std::move(task),5);

    std::cout << "resultado: " << fut.get() << std::endl;

    t.join();

    // Another task

    std::packaged_task<int(int, int)> sum_task(sum);

    std::future<int> fut_sum = sum_task.get_future();

    std::thread t_sum(std::move(sum_task),5,4);

    std::cout << "resultado sum: " << fut_sum.get() << std::endl;

    t_sum.join();
}