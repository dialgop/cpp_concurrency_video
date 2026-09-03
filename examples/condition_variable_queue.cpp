#include <thread>
#include <iostream>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>

std::queue<int> data_queue;
std::mutex mtx;
std::condition_variable cv;
bool finished = false; //To indicate the consumer that there will not be anymore data

void producer() {
    for(int i=1; i <=5; ++i) {
        {
            std::lock_guard<std::mutex> lock(mtx); //lock_guard for short access
            data_queue.push(i);
            std::cout <<"[producer] Pushed: " << i << std::endl;
        }
        cv.notify_one(); // tells the consumer
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    //ending signal
    {
        std::lock_guard<std::mutex> lock(mtx);
        finished = true;
    }
    cv.notify_all();
}

void consumer(int id) {
    while (true) {
        //lock_guard for long waitings -> can give the lock away for some time to condition_variable, lock_guard cannot do it
        std::unique_lock<std::mutex> lock(mtx);
         cv.wait(lock, [] {return !data_queue.empty() || finished;}); //cv.wait(lock, predicate)

        if (!data_queue.empty()) {
            int value = data_queue.front();
            data_queue.pop();
            lock.unlock(); // Frees the mutex before processing
            std::cout << " [consumer] " << id << " Popped " << value << std::endl;
        }
        else if (finished) {
            break; //Leaves the loop
        }
    }

    std::cout << " [consumer] finished" << std::endl;

}

int main() {

    std::thread t1(producer);
    std::thread t2(consumer, 2);
    std::thread t3(consumer, 3);

    t1.join();
    t2.join();
    t3.join();

    std::cout << "Done" << std::endl;
}
