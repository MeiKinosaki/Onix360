#include "wx360/memory/powerpc_hashed_page_table.hpp"
#include <array>
#include <cstdlib>
#include <iostream>
using namespace wx360::memory;
int main() {
    PhysicalMemory ram(1U << 20U);
    PowerPcHashedPageTable table;
    int failures = 0;
    auto check = [&](bool ok, const char* label) {
        if (!ok) { std::cerr << "FAIL: " << label << '\n'; ++failures; }
    };
    check(table.configure(0x10000, 1024, ram) == MemoryResult::Ok,
          "configures aligned power-of-two HTAB");
    constexpr std::uint32_t vsid = 0x123456U;
    constexpr std::uint64_t ea = 0x12345ABCULL;
    check(table.insert(vsid, ea, 0x8000, 2, ram) == MemoryResult::Ok,
          "inserts primary PTE");
    auto hit = table.lookup(vsid, ea, ram);
    check(hit.result == MemoryResult::Ok && hit.physical_address == 0x8ABC &&
          hit.pp == 2 && !hit.secondary,
          "walks primary PTE and preserves page offset and PP");
    check(table.lookup(vsid + 1U, ea, ram).result == MemoryResult::Unmapped,
          "rejects PTE with mismatched VSID");
    check(table.mark_access(hit, Access::Read, ram) == MemoryResult::Ok,
          "marks a read reference in PTE1");
    std::array<std::uint8_t,4> pte1_bytes{};
    check(ram.read(hit.pte_address + 4U, pte1_bytes) == MemoryResult::Ok &&
          (pte1_bytes[2] & 0x08U) != 0U &&
          (pte1_bytes[2] & 0x04U) == 0U,
          "read sets R but leaves C clear");
    check(table.mark_access(hit, Access::Write, ram) == MemoryResult::Ok,
          "marks a write change in PTE1");
    check(ram.read(hit.pte_address + 4U, pte1_bytes) == MemoryResult::Ok &&
          (pte1_bytes[2] & 0x0CU) == 0x0CU,
          "write preserves R and sets C");
    PowerPcHashedPageTable sdr1_table;
    check(sdr1_table.configure_from_sdr1(0x00010000U, ram) == MemoryResult::Ok &&
          sdr1_table.group_count() == 1024,
          "decodes SDR1 origin and minimum HTAB size");
    check(sdr1_table.configure_from_sdr1(0x00010005U, ram) ==
              MemoryResult::AlignmentFault,
          "rejects non-contiguous SDR1 HTAB mask");
    check(table.insert(vsid, ea + 1U, 0x9001, 2, ram) == MemoryResult::AlignmentFault,
          "rejects unaligned physical page");

    const auto hash = PowerPcHashedPageTable::page_hash(vsid, ea);
    const auto primary_group = 0x10000U +
        (hash & 1023U) * PowerPcHashedPageTable::entries_per_group *
        PowerPcHashedPageTable::pte_size;
    for (std::size_t i = 0; i < 8; ++i) {
        const std::uint32_t valid = 0x80000000U | static_cast<std::uint32_t>(i + 1U);
        const std::array<std::uint8_t,4> bytes{
            std::uint8_t(valid >> 24U), std::uint8_t(valid >> 16U),
            std::uint8_t(valid >> 8U), std::uint8_t(valid)};
        check(ram.write(primary_group + i * 8U, bytes) == MemoryResult::Ok,
              "fills primary PTEG for secondary lookup test");
    }
    constexpr std::uint64_t secondary_ea = 0x23456DEFULL;
    const auto secondary_hash = PowerPcHashedPageTable::page_hash(vsid, secondary_ea);
    const auto secondary_primary_group = 0x10000U +
        (secondary_hash & 1023U) * PowerPcHashedPageTable::entries_per_group *
        PowerPcHashedPageTable::pte_size;
    for (std::size_t i = 0; i < 8; ++i) {
        const std::uint32_t valid = 0x80000000U | static_cast<std::uint32_t>(i + 1U);
        const std::array<std::uint8_t,4> bytes{
            std::uint8_t(valid >> 24U), std::uint8_t(valid >> 16U),
            std::uint8_t(valid >> 8U), std::uint8_t(valid)};
        check(ram.write(secondary_primary_group + i * 8U, bytes) == MemoryResult::Ok,
              "fills primary group for secondary insertion");
    }
    check(table.insert(vsid, secondary_ea, 0xA000, 1, ram) == MemoryResult::Ok,
          "inserts PTE in secondary group when primary is full");
    auto secondary = table.lookup(vsid, secondary_ea, ram);
    check(secondary.result == MemoryResult::Ok &&
          secondary.physical_address == 0xADEF &&
          secondary.secondary && secondary.pp == 1,
          "walks secondary PTEG and reports secondary match");
    check(table.configure(0x1001, 1024, ram) == MemoryResult::AlignmentFault,
          "rejects unaligned HTAB base");
    check(table.configure(0x10000, 1025, ram) == MemoryResult::AlignmentFault,
          "rejects non-power-of-two PTEG count");
    check(table.configure(0x100000, 1024, ram) == MemoryResult::OutOfRange,
          "rejects HTAB outside physical RAM");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
