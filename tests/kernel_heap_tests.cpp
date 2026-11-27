#include "wx360/kernel/kernel_heap.hpp"

#include <cstdlib>
#include <iostream>

int main() {
    wx360::memory::PhysicalMemory ram(0x10000);
    wx360::kernel::KernelHeap heap(ram, 0x2000, 0x4000);
    int failures = 0;
    const auto check = [&](bool value, const char* message) {
        if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };

    check(heap.valid(), "heap accepts a fully mapped RAM interval");
    const auto first = heap.allocate(100, 64);
    const auto second = heap.allocate(200, 16);
    check(first.has_value() && (*first % 64U) == 0, "allocation honors power-of-two alignment");
    check(second.has_value() && *second >= *first + 100U, "first-fit allocations do not overlap");
    check(!heap.allocate(1, 3).has_value(), "rejects non-power-of-two alignment");
    check(!heap.allocate(0, 16).has_value(), "rejects zero-byte allocation");
    check(first.has_value() && heap.release(*first), "releases exact allocation base");
    check(first.has_value() && !heap.release(*first), "rejects double free");
    check(second.has_value() && heap.release(*second), "releases adjacent allocation");
    check(heap.allocation_count() == 0, "all allocations released");
    const auto whole = heap.allocate(0x4000, 16);
    check(whole.has_value() && *whole == 0x2000, "coalesces free blocks into original extent");
    check(whole.has_value() && heap.release(*whole), "releases coalesced full heap");

    wx360::kernel::KernelHeap invalid(ram, 0xF000, 0x2000);
    check(!invalid.valid() && !invalid.allocate(16).has_value(),
          "rejects heap interval outside physical RAM");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
