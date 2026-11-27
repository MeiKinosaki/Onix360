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
    check(heap.validate() && heap.free_bytes() == 0x4000 &&
          heap.largest_free_block() == 0x4000,
          "initial heap accounting covers the complete extent");
    check(heap.owns_address(0x2000) && heap.owns_address(0x5FFF) &&
          !heap.owns_address(0x6000) && !heap.owns_address(0x1000),
          "heap ownership uses a half-open address range");
    const auto first = heap.allocate(100, 64);
    const auto second = heap.allocate(200, 16);
    check(first.has_value() && (*first % 64U) == 0, "allocation honors power-of-two alignment");
    check(second.has_value() && *second >= *first + 100U, "first-fit allocations do not overlap");
    check(!heap.allocate(1, 3).has_value(), "rejects non-power-of-two alignment");
    check(!heap.allocate(0, 16).has_value(), "rejects zero-byte allocation");
    check(heap.validate() && heap.free_bytes() < 0x4000 &&
          heap.largest_free_block() <= heap.free_bytes(),
          "allocation accounting remains internally consistent");
    check(first.has_value() && heap.release(*first), "releases exact allocation base");
    check(first.has_value() && !heap.release(*first), "rejects double free");
    check(second.has_value() && heap.release(*second), "releases adjacent allocation");
    check(heap.allocation_count() == 0, "all allocations released");
    check(heap.validate() && heap.free_bytes() == 0x4000 &&
          heap.largest_free_block() == 0x4000,
          "release coalesces and restores the complete free extent");
    const auto whole = heap.allocate(0x4000, 16);
    check(whole.has_value() && *whole == 0x2000, "coalesces free blocks into original extent");
    check(whole.has_value() && heap.release(*whole), "releases coalesced full heap");
    check(heap.validate(), "heap invariant holds after full allocation cycle");

    wx360::kernel::KernelHeap invalid(ram, 0xF000, 0x2000);
    check(!invalid.valid() && !invalid.allocate(16).has_value() &&
          !invalid.validate() && invalid.free_bytes() == 0,
          "rejects heap interval outside physical RAM");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
