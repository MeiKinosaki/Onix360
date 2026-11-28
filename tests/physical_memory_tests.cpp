#include "onix360/memory/physical_memory.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main() {
    using namespace onix360::memory;
    PhysicalMemory ram(PhysicalMemory::page_size * 2U);
    const std::array<std::uint8_t, 4> instruction{0x38, 0x60, 0x00, 0x01};
    check(ram.write(0x100, instruction) == MemoryResult::Ok,
          "write bytes into physical RAM");
    std::uint32_t fetched = 0;
    check(ram.fetch_u32(0x100, fetched) == MemoryResult::Ok,
          "fetch instruction from executable page");
    check(fetched == 0x38600001U, "instruction fetch uses PowerPC big-endian order");
    check(ram.fetch_u32(0x101, fetched) == MemoryResult::AlignmentFault,
          "reject instruction fetch not aligned to four bytes");

    check(ram.set_page_permissions(0, PhysicalMemory::page_size,
          static_cast<std::uint8_t>(Access::Read) |
          static_cast<std::uint8_t>(Access::Execute)) == MemoryResult::Ok,
          "set read/execute page permissions");
    std::array<std::uint8_t, 4> output{};
    check(ram.read(0x100, output) == MemoryResult::Ok,
          "read permitted page");
    check(ram.write(0x100, instruction) == MemoryResult::PermissionDenied,
          "reject write to read-only page");
    check(ram.fetch_u32(0x100, fetched) == MemoryResult::Ok,
          "allow instruction fetch from executable page");
    check(ram.fetch_u32(PhysicalMemory::page_size, fetched) == MemoryResult::Ok,
          "second page remains executable by default");
    check(ram.read(ram.size() - 2U, output) == MemoryResult::OutOfRange,
          "reject read crossing physical RAM boundary");
    check(ram.write(ram.size(), instruction) == MemoryResult::OutOfRange,
          "reject write at end of physical RAM");
    if (failures == 0) std::cout << "Onix360 physical memory tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
