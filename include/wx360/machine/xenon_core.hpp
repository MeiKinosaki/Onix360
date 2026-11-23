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
    InstructionAlignmentFault,
    DataAlignmentFault,
    ExceptionTaken,
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
        if (fetch == memory::MemoryResult::AlignmentFault) {
            state.raise_exception(0x600U, state.pc);
            return RunResult::ExceptionTaken;
        }
        if (fetch != memory::MemoryResult::Ok) {
            state.raise_exception(0x400U, state.pc);
            return RunResult::ExceptionTaken;
        }
        std::uint64_t fault_address = 0;
        const auto result = cpu::execute(state, instruction, ram.raw_bytes(), &fault_address);
        switch (result) {
        case cpu::StepResult::Executed: return RunResult::Executed;
        case cpu::StepResult::UnsupportedInstruction:
            state.raise_exception(0x700U);
            return RunResult::ExceptionTaken;
        case cpu::StepResult::MemoryFault:
            state.raise_exception(0x300U, fault_address);
            return RunResult::ExceptionTaken;
        case cpu::StepResult::AlignmentFault:
            state.raise_exception(0x600U, fault_address);
            return RunResult::ExceptionTaken;
        }
        return RunResult::UnsupportedInstruction;
    }
};

} // namespace wx360::machine
