#pragma once

#include "wx360/cpu/powerpc_interpreter.hpp"
#include "wx360/memory/physical_memory.hpp"
#include "wx360/memory/address_space.hpp"
#include "wx360/kernel/kernel_runtime.hpp"
#include "wx360/xex/xex_image_loader.hpp"\n#include "wx360/audio/audio_engine.hpp"\n#include "wx360/input/controller_state.hpp"\n#include "wx360/gpu/xenos_command_processor.hpp"

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
    kernel::KernelRuntime* kernel_runtime{nullptr};
    memory::AddressSpace address_space{};
    bool mmu_enabled{false};

    [[nodiscard]] memory::MemoryResult map_guest_page(std::uint64_t virtual_address,
        std::uint64_t physical_address, std::uint8_t permissions) noexcept {
        const auto result = address_space.map_page(virtual_address, physical_address, permissions);
        if (result == memory::MemoryResult::Ok) mmu_enabled = true;
        return result;
    }

    struct InterpreterBusContext {
        XenonCore* core{};
        memory::PhysicalMemory* ram{};
    };

    [[nodiscard]] static bool interpreter_read(
        void* opaque, std::uint64_t guest_address,
        std::span<std::uint8_t> output) noexcept {
        auto& context = *static_cast<InterpreterBusContext*>(opaque);
        auto& core = *context.core;
        auto& ram = *context.ram;
        if (!core.mmu_enabled)
            return ram.read(guest_address, output, memory::Access::Read) == memory::MemoryResult::Ok;
        const auto translated = core.address_space.translate_range(
            guest_address, output.size(), memory::Access::Read, ram);
        return translated.result == memory::MemoryResult::Ok &&
            ram.read(translated.physical_address, output, memory::Access::Read) == memory::MemoryResult::Ok;
    }

    [[nodiscard]] static bool interpreter_write(
        void* opaque, std::uint64_t guest_address,
        std::span<const std::uint8_t> input) noexcept {
        auto& context = *static_cast<InterpreterBusContext*>(opaque);
        auto& core = *context.core;
        auto& ram = *context.ram;
        if (!core.mmu_enabled)
            return ram.write(guest_address, input) == memory::MemoryResult::Ok;
        const auto translated = core.address_space.translate_range(
            guest_address, input.size(), memory::Access::Write, ram);
        return translated.result == memory::MemoryResult::Ok &&
            ram.write(translated.physical_address, input) == memory::MemoryResult::Ok;
    }

    // Only the loader-minted validation token can start execution.
    // Explicit guest data accessors share the same translator as instruction fetch.
    [[nodiscard]] memory::MemoryResult read_guest(
        memory::PhysicalMemory& ram, std::uint64_t guest_address,
        std::span<std::uint8_t> output) const noexcept {
        std::uint64_t physical_address = guest_address;
        if (mmu_enabled) {
            const auto translated = address_space.translate_range(
                guest_address, output.size(), memory::Access::Read, ram);
            if (translated.result != memory::MemoryResult::Ok) return translated.result;
            physical_address = translated.physical_address;
        }
        return ram.read(physical_address, output, memory::Access::Read);
    }

    [[nodiscard]] memory::MemoryResult write_guest(
        memory::PhysicalMemory& ram, std::uint64_t guest_address,
        std::span<const std::uint8_t> input) const noexcept {
        std::uint64_t physical_address = guest_address;
        if (mmu_enabled) {
            const auto translated = address_space.translate_range(
                guest_address, input.size(), memory::Access::Write, ram);
            if (translated.result != memory::MemoryResult::Ok) return translated.result;
            physical_address = translated.physical_address;
        }
        return ram.write(physical_address, input);
    }

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
        std::uint64_t fetch_address = state.pc;
        if (mmu_enabled) {
            const auto translated = address_space.translate(state.pc, memory::Access::Execute, ram);
            if (translated.result != memory::MemoryResult::Ok) {
                state.raise_exception(0x400U, state.pc);
                return RunResult::ExceptionTaken;
            }
            fetch_address = translated.physical_address;
        }
        const auto fetch = ram.fetch_u32(fetch_address, instruction);
        if (fetch == memory::MemoryResult::AlignmentFault) {
            state.raise_exception(0x600U, state.pc);
            return RunResult::ExceptionTaken;
        }
        if (fetch != memory::MemoryResult::Ok) {
            state.raise_exception(0x400U, state.pc);
            return RunResult::ExceptionTaken;
        }
        if (instruction == 0x44000002U) { // sc: host kernel dispatch bridge
            if (kernel_runtime && kernel_runtime->dispatch(state)) {
                state.pc += 4U;
                return RunResult::Executed;
            }
            state.raise_exception(0xC00U);
            return RunResult::ExceptionTaken;
        }
        std::uint64_t fault_address = 0;
        InterpreterBusContext bus_context{this, &ram};
        const cpu::MemoryAccess bus{&bus_context, &XenonCore::interpreter_read,
                                    &XenonCore::interpreter_write};
        const auto result = cpu::execute(state, instruction, ram.raw_bytes(),
                                         &fault_address, &bus);
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
