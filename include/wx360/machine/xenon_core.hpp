#pragma once

#include "wx360/cpu/powerpc_interpreter.hpp"
#include "wx360/memory/physical_memory.hpp"

namespace wx360::machine {

enum class RunResult : std::uint8_t {
    Executed,
    FetchFault,
    UnsupportedInstruction,
    DataMemoryFault,
};

struct XenonCore {
    cpu::XenonThreadState state{};

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
