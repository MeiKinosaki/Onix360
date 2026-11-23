#pragma once

#include "wx360/cpu/powerpc_interpreter.hpp"
#include "wx360/memory/physical_memory.hpp"
#include "wx360/xex/xex_image_loader.hpp"

namespace wx360::machine {

enum class RunResult : std::uint8_t {
    Executed,
    FetchFault,
    UnsupportedInstruction,
    DataMemoryFault,
};

enum class LaunchResult : std::uint8_t { Started, ImageNotMapped, EntryPointNotExecutable };

struct XenonCore {
    cpu::XenonThreadState state{};

    // XEX decryption/decompression, PE mapping and kernel imports must be
    // completed before transferring control to the Xenon interpreter.
    [[nodiscard]] LaunchResult start_xex(
        const xex::LaunchPlan& plan, memory::PhysicalMemory& ram) noexcept {
        if (!plan.decryption_complete || !plan.decompression_complete ||
            !plan.pe_mapped || !plan.kernel_imports_resolved)
            return LaunchResult::ImageNotMapped;
        std::uint32_t first_instruction = 0;
        if (ram.fetch_u32(plan.mapped_entry_point, first_instruction) != memory::MemoryResult::Ok)
            return LaunchResult::EntryPointNotExecutable;
        (void)first_instruction;
        state.reset(plan.mapped_entry_point);
        return LaunchResult::Started;
    }

    [[nodiscard]] RunResult step(memory::PhysicalMemory& ram) noexcept {
        std::uint32_t instruction = 0;
        const auto fetch = ram.fetch_u32(state.pc, instruction);
        if (fetch != memory::MemoryResult::Ok) return RunResult::FetchFault;
        const auto result = cpu::execute(state, instruction, ram.raw_bytes());
        switch (result) {
        case cpu::StepResult::Executed: return RunResult::Executed;
        case cpu::StepResult::UnsupportedInstruction: return RunResult::UnsupportedInstruction;
        case cpu::StepResult::MemoryFault: return RunResult::DataMemoryFault;
        }
        return RunResult::UnsupportedInstruction;
    }
};

} // namespace wx360::machine
