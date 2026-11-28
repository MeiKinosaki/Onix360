#include "onix360/kernel/kernel_heap.hpp"
#include "onix360/kernel/kernel_runtime.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

int main() {
    using namespace onix360;
    memory::PhysicalMemory ram(0x10000);
    kernel::KernelHeap heap(ram, 0x4000, 0x4000);
    kernel::KernelRuntime runtime;
    int failures = 0;
    const auto check = [&](bool value, const char* message) {
        if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };

    check(runtime.register_kernel_heap_exports(heap, 0x100, 0x101,
          0x200, 0x220, ram), "registers metadata-verified ExAllocatePool and ExFreePool");
    check(!runtime.register_kernel_heap_exports(heap, 0x102, 0x103,
          0x240, 0x260, ram), "rejects duplicate heap service registration");

    cpu::XenonThreadState state{};
    state.gpr[0] = 0x100; // internal dispatcher ID, not the guest export ordinal
    state.gpr[3] = 0;     // NonPagedPool
    state.gpr[4] = 256;   // NumberOfBytes
    check(runtime.dispatch(state), "dispatches ExAllocatePool service");
    const auto allocation = static_cast<std::uint32_t>(state.gpr[3]);
    check(allocation >= heap.base() && allocation < heap.base() + heap.size(),
          "returns guest address from the bounded heap");
    check(heap.allocation_count() == 1, "allocation is tracked by the kernel heap");

    state.gpr[0] = 0x101;
    state.gpr[3] = allocation;
    check(runtime.dispatch(state), "dispatches ExFreePool service");
    check(heap.allocation_count() == 0, "ExFreePool releases the guest allocation");

    state.gpr[0] = 0x100;
    state.gpr[3] = 0xFFFFFFFFU; // unsupported pool type
    state.gpr[4] = 64;
    check(runtime.dispatch(state) && state.gpr[3] == 0,
          "unsupported pool type fails with null allocation");

    std::array<std::uint8_t, 12> allocate_stub{};
    std::array<std::uint8_t, 12> free_stub{};
    check(ram.read(0x200, allocate_stub) == memory::MemoryResult::Ok &&
          allocate_stub[0] == 0x38 && allocate_stub[1] == 0 && allocate_stub[2] == 1 &&
          allocate_stub[3] == 0, "emits guest trampoline for allocation export");
    check(ram.read(0x220, free_stub) == memory::MemoryResult::Ok &&
          free_stub[0] == 0x38 && free_stub[1] == 0 && free_stub[2] == 1 &&
          free_stub[3] == 1, "emits guest trampoline for free export");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
