// Stage 3: controller / state machine under concurrent commands.
//
// Drives a PipelineController (include/pipeline_controller.hpp) through
// start / seek / simulated-fault / recover / stop, printing status queried
// between each step. All of the controller's internal state is private to
// its own owner thread; this file never touches it directly, only ever
// through submit()/query_status() messages.

#include "pipeline_controller.hpp"

#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

namespace {

void print_status(const char* label, const Status& s) {
    std::cout << "  [" << label << "] running=" << std::boolalpha << s.running << " faulted=" << s.faulted
               << " emitted=" << s.frames_emitted << " delay=" << s.current_delay.count()
               << "ms recoveries=" << s.recoveries << "\n";
}

} // namespace

int main() {
    std::cout << "Stage 3: actor-style controller (command queue, no shared-state locks)\n\n";

    PipelineController controller(100ms);

    std::cout << "starting...\n";
    controller.start();
    std::this_thread::sleep_for(300ms);
    print_status("running 300ms", controller.query_status());

    std::cout << "\nseeking delay 100ms -> 50ms...\n";
    controller.seek(50ms);
    std::this_thread::sleep_for(300ms);
    print_status("after seek + 300ms", controller.query_status());

    std::cout << "\nsimulating a stuck feed...\n";
    controller.simulate_fault();
    std::this_thread::sleep_for(300ms);
    print_status("while faulted", controller.query_status());

    std::cout << "\nrecovering...\n";
    controller.recover();
    std::this_thread::sleep_for(300ms);
    print_status("after recover + 300ms", controller.query_status());

    std::cout << "\nstopping...\n";
    controller.stop();
    std::this_thread::sleep_for(100ms);
    print_status("after stop", controller.query_status());

    std::cout << "\nDone.\n";
    // controller's destructor shuts down its owner + clock threads here.
}
