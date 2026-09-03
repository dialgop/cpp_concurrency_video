#include <iostream>
#include <thread>
#include <future>

void producer(std::promise<int> p) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    p.set_value(42);
}

void consumer(std::future<int> f) {
    int value = f.get(); // Waits until set_value is called
    std::cout << "received vaulue: " << value << std::endl;
}

int main() {
    // Each promise can be called get_future() only once. If it's done again, it throws std::future_error
    std::promise<int> p; // writer side

    std::future<int> f = p.get_future(); //reader side -> p.get_future establishes the connection between p and f
    //For several reads and avoid losing the chance to called future several times use shared_future (auto sf = f.share())
    // std::shared_future<int> sf = f.share() y en los threads se hace un lambda call
    // ....
    // std::thread t1([sf]() { std::cout << "Hilo 1: " << sf.get() << "\n"; });
    // std::thread t2([sf]() { std::cout << "Hilo 2: " << sf.get() << "\n"; });


    //Move because promise and future are not to copy, only to move. The objects p and f in main obtain an invalid state
    //but the shared state still exists and it is the same for both threads

    std::thread t1(producer, std::move(p));
    //If consumer calls f.get() before producer make p.set_value(42) it blocks itself until the value is ready
    std::thread t2(consumer, std::move(f));

    t1.join();
    t2.join();


}