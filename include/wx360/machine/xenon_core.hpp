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

enum class LaunchResult : std::uint8_t { Started, EntryPointNotExecutable };

struct XenonCore {
    cpu::XenonThreadState state{};

    // Only the loader-minted validation token can start execution.
    [[nodiscard]] LaunchResult start_xex(
        const xex::ValidatedXexImage& image, memory::PhysicalMemory& ram) noexcept {
        std::uint32_t first_instruction = 0;
        if (ram.fetch_u32(image.entry_point(), first_instruction) != memory::MemoryResult::Ok)
            return LaunchResult::EntryPointNotExecutable;
        state.reset(image.entry_point());
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
