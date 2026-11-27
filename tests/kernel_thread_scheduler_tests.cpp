#include "wx360/kernel/kernel_thread_scheduler.hpp"

#include <cstdlib>
#include <iostream>

int main() {
    using namespace wx360;
    int failures = 0;
    const auto check = [&](bool ok, const char* label) {
        if (!ok) { std::cerr << "FAIL: " << label << '\n'; ++failures; }
    };

    kernel::KernelThreadScheduler scheduler(3);
    cpu::XenonThreadState a{}, b{}, c{}, live{};
    a.reset(0x1000); b.reset(0x2000); c.reset(0x3000);
    a.gpr[3] = 11; b.gpr[3] = 22; c.gpr[3] = 33;
    const auto id_a = scheduler.create(a, 8);
    const auto id_b = scheduler.create(b, 16);
    const auto id_c = scheduler.create(c, 24);
    check(id_a && id_b && id_c, "creates guest threads within capacity");
    check(!scheduler.create(a), "rejects thread creation beyond capacity");
    check(scheduler.schedule(live) && live.pc == 0x3000 &&
          scheduler.current_id() == id_c, "dispatches highest-priority ready guest thread");

    live.pc = 0x3010; live.gpr[3] = 333;
    check(scheduler.yield(live) && live.pc == 0x2000,
          "yield saves context and selects next-highest-priority thread");
    check(scheduler.find(*id_c)->context.pc == 0x3010 &&
          scheduler.find(*id_c)->context.gpr[3] == 333,
          "preserves full guest register context across yield");

    live.pc = 0x2020;
    check(scheduler.block_current(live) && live.pc == 0x1000 &&
          scheduler.find(*id_b)->status == kernel::ThreadStatus::Blocked,
          "blocking current thread schedules another ready thread");
    check(!scheduler.unblock(*id_a), "cannot unblock a non-blocked thread");
    check(scheduler.unblock(*id_b), "unblocks a blocked guest thread");

    check(scheduler.terminate_current(live) &&
          scheduler.find(*id_a)->status == kernel::ThreadStatus::Terminated &&
          live.pc == 0x3010,
          "terminates current thread and schedules highest-priority ready context");
    check(scheduler.current_id() == id_c, "highest-priority yielded thread resumes");
    check(scheduler.terminate_current(live) && live.pc == 0x2020,
          "terminates yielded thread and dispatches unblocked thread");
    check(scheduler.find(*id_b)->status == kernel::ThreadStatus::Running,
          "unblocked thread returns to running state");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
