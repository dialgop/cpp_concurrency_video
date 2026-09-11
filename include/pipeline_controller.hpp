#pragma once

#include "command_queue.hpp"
#include "delay_line_mutex.hpp"

#include <chrono>
#include <future>
#include <optional>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <variant>

// Stage 3: a controller / state machine driven entirely by commands.
//
// Stage 1 and 2 both shared state across threads and used a lock (mutex) or
// a rendezvous (barrier) to keep that safe. This stage takes the opposite
// approach: exactly one thread - the "owner" - ever touches the pipeline's
// state (running/faulted flags, current delay, frame counters). Every other
// thread can only ask it to do something, or ask it a question, by sending
// a message through a CommandQueue. Because nothing outside owner_loop()
// ever reads or writes that state, none of it needs a mutex at all - the
// queue is the only shared thing, and it's generic infrastructure, not
// pipeline-specific state. This is the "actor model": one thread owns data,
// everyone else talks to it via messages.
struct StartCommand {};
struct StopCommand {};
struct SeekCommand {
    std::chrono::milliseconds new_delay;
};
struct SimulateFaultCommand {}; // pretend the feed has hung, for Recover to fix
struct RecoverCommand {};

struct Status {
    bool running = false;
    bool faulted = false;
    int frames_emitted = 0;
    std::chrono::milliseconds current_delay{0};
    int recoveries = 0;
};

// Even a *read* goes through the queue: the caller hands over a promise, the
// owner thread fills it in, the caller blocks on the matching future. This
// is the strictest version of "ask, don't touch" - status() is not a
// direct accessor into shared state, because there is no shared state to
// access.
struct StatusQuery {
    std::promise<Status> result;
};

// Driven periodically by our own internal clock thread rather than by an
// external caller - but it flows through the exact same queue as every
// other command above. There's no separate "internal event" path, which is
// what makes this a genuine single message loop rather than a queue plus a
// side channel.
struct TickCommand {};

using Command = std::variant<StartCommand, StopCommand, SeekCommand, SimulateFaultCommand, RecoverCommand,
                              StatusQuery, TickCommand>;

class PipelineController {
public:
    explicit PipelineController(std::chrono::milliseconds initial_delay,
                                 std::chrono::milliseconds tick_interval = std::chrono::milliseconds(33))
        // Declaration order (see below) constructs commands_ first, then
        // clock_, then owner_ - so by the time owner_loop starts running,
        // the queue it depends on already exists.
        : clock_([this, tick_interval](std::stop_token st) { clock_loop(st, tick_interval); }),
          owner_([this, initial_delay](std::stop_token st) { owner_loop(st, initial_delay); }) {}

    // No destructor needed: std::jthread's own destructor already calls
    // request_stop() then join() for us - the same cooperative-cancellation
    // mechanism from examples/jthread_stop_token.cpp, reused here purely to
    // tear the actor's threads down cleanly. That's infrastructure lifecycle,
    // separate from the Start/Stop/Seek/Recover *domain* commands below,
    // which is what this stage is actually about.
    //
    // Members are declared [commands_, clock_, owner_], so destruction runs
    // in reverse: owner_ is stopped and joined first, while clock_ is still
    // ticking - guaranteeing owner_loop's blocking pop() gets woken by a
    // TickCommand within one tick_interval so it can notice stop_requested()
    // and exit. Only then is clock_ itself stopped.

    void submit(Command cmd) { commands_.push(std::move(cmd)); }

    void start() { submit(StartCommand{}); }
    void stop() { submit(StopCommand{}); }
    void seek(std::chrono::milliseconds new_delay) { submit(SeekCommand{new_delay}); }
    void simulate_fault() { submit(SimulateFaultCommand{}); }
    void recover() { submit(RecoverCommand{}); }

    Status query_status() {
        std::promise<Status> promise;
        auto future = promise.get_future();
        submit(StatusQuery{std::move(promise)});
        return future.get();
    }

private:
    void clock_loop(std::stop_token st, std::chrono::milliseconds interval) {
        while (!st.stop_requested()) {
            std::this_thread::sleep_for(interval);
            commands_.push(TickCommand{});
        }
    }

    void owner_loop(std::stop_token st, std::chrono::milliseconds initial_delay) {
        // Everything below is local to this one thread. Nothing outside
        // this function ever touches it, so none of it is behind a mutex.
        bool running = false;
        bool faulted = false;
        int frame_id = 0;
        int frames_emitted = 0;
        int recoveries = 0;
        auto delay = initial_delay;

        // Reused (not re-derived) from Stage 1. optional<> lets Seek/Recover
        // rebuild it in place with a new delay - DelayLineMutex itself can't
        // be move-assigned since it holds a std::mutex - dropping whatever
        // was in flight, same way a real feed reconnect would.
        std::optional<DelayLineMutex<int>> line;
        line.emplace(delay, /*capacity=*/8);

        while (!st.stop_requested()) {
            Command cmd = commands_.pop(); // blocks; woken at least every tick_interval by a TickCommand
            std::visit(
                [&](auto&& c) {
                    using C = std::decay_t<decltype(c)>;
                    if constexpr (std::is_same_v<C, StartCommand>) {
                        running = true;
                    } else if constexpr (std::is_same_v<C, StopCommand>) {
                        running = false;
                    } else if constexpr (std::is_same_v<C, SeekCommand>) {
                        delay = c.new_delay;
                        line.emplace(delay, /*capacity=*/8);
                    } else if constexpr (std::is_same_v<C, SimulateFaultCommand>) {
                        faulted = true;
                    } else if constexpr (std::is_same_v<C, RecoverCommand>) {
                        faulted = false;
                        ++recoveries;
                        line.emplace(delay, /*capacity=*/8); // as if the feed reconnected from scratch
                    } else if constexpr (std::is_same_v<C, StatusQuery>) {
                        c.result.set_value(Status{running, faulted, frames_emitted, delay, recoveries});
                    } else if constexpr (std::is_same_v<C, TickCommand>) {
                        if (running && !faulted) {
                            line->push(frame_id++);
                        }
                        while (line->try_pop()) {
                            ++frames_emitted;
                        }
                    } else {
                        static_assert(!sizeof(C*), "unhandled Command alternative");
                    }
                },
                cmd);
        }
    }

    CommandQueue<Command> commands_;
    std::jthread clock_;
    std::jthread owner_;
};
