# cpp-concurrency-video

A hands-on tour of C++20 concurrency, built as a set of small standalone
examples plus one evolving project that composes the pieces into something
closer to a real system (loosely modeled on broadcast pipeline patterns:
delay lines, multi-source fusion, controllers, thread pools, watchdogs).

## Layout

- **`examples/`** — one file per concurrency primitive. Each is a
  self-contained `main()` you can build and run independently:
  - `thread_basics.cpp` — `std::thread` with a manually shared `atomic<bool>` stop flag
  - `jthread_stop_token.cpp` — the same demo using `std::jthread` + `std::stop_token`, for comparison
  - `mutex_lockguard.cpp` — protecting shared state with `std::mutex` + `std::lock_guard`
  - `condition_variable_queue.cpp` — producer/consumer signaling with `std::condition_variable`
  - `promise_future.cpp` — `std::promise` / `std::future`
  - `async_future.cpp` — `std::async`
  - `packaged_task.cpp` — `std::packaged_task`

- **`include/`** — reusable components for the composed project. Each stage
  below adds headers here that later stages build on.

- **`stages/`** — one runnable demo per stage of the composed project.

## Composed project stages

1. **Delay-line ring buffer** (`stages/stage1_delay_line.cpp`) — a producer
   thread writes frames, a consumer thread reads each one back only after a
   fixed delay has elapsed, simulating a broadcast delay line. Two
   implementations are compared:
   - `include/delay_line_mutex.hpp` — mutex + `condition_variable`
   - `include/delay_line_lockfree.hpp` — lock-free single-producer/
     single-consumer ring buffer built on `std::atomic` with explicit
     `memory_order_acquire`/`memory_order_release`, plus C++20 atomic
     wait/notify instead of busy-spinning.
2. **Multi-source fusion with barriers** — synchronizing several capture
   threads at a rendezvous point with `std::barrier`/`std::latch` before a
   fusion thread combines their output. *(planned)*
3. **Controller / state machine** — routing commands through an actor-style
   message queue (`std::variant`-based) instead of exposing shared state
   directly. *(planned)*
4. **Thread pool** — a reusable pool (`std::jthread`s + work queue) for
   running Stage 2/3 work concurrently across multiple feeds. *(planned)*
5. **Watchdog / health monitoring** — a supervisor thread that detects and
   restarts a hung worker using `condition_variable::wait_for` timeouts.
   *(planned)*

## Building

Requires a C++20 compiler and CMake 3.20+.

```sh
cmake -S . -B build
cmake --build build -j
```

This produces one executable per file in `examples/` (prefixed
`example_`) and one per file in `stages/`, e.g.:

```sh
./build/stage1_delay_line
./build/example_jthread_stop_token
```