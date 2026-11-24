#pragma once
#include "wx360/cpu/xenon_state.hpp"
#include "wx360/kernel/xex_import_resolver.hpp"
#include "wx360/memory/physical_memory.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>

namespace wx360::kernel {

// Small host-backed system-call table and guest-visible PowerPC SC stubs.
// This is a development bridge, not a complete xboxkrnl/xam implementation.
class KernelRuntime {
public:
    using Handler = void (*)(cpu::XenonThreadState&) noexcept;

    [[nodiscard]] bool register_service(std::uint16_t id, Handler handler) {
        return handler && services_.emplace(id, handler).second;
    }
    [[nodiscard]] bool dispatch(cpu::XenonThreadState& state) const noexcept {
        const auto id = static_cast<std::uint16_t>(state.gpr[0] & 0xFFFFU);
        const auto it = services_.find(id);
        if (it == services_.end()) return false;
        it->second(state);
        return true;
    }
    [[nodiscard]] bool emit_trampoline(std::string library, std::uint16_t ordinal,
        std::uint16_t service_id, std::uint32_t guest_address,
        memory::PhysicalMemory& ram) {
        if (service_id > 0x7FFFU || guest_address == 0 ||
            ram.check_access(guest_address, 12, memory::Access::Write) != memory::MemoryResult::Ok)
            return false;
        const std::uint32_t words[3] = {
            0x38000000U | service_id, // li r0, service_id
            0x44000002U,              // sc
            0x4E800020U               // blr
        };
        std::array<std::uint8_t,12> bytes{};
        for (unsigned w=0;w<3;++w) for(unsigned b=0;b<4;++b)
            bytes[w*4U+b]=static_cast<std::uint8_t>(words[w] >> (24U-b*8U));
        if (ram.write(guest_address, bytes) != memory::MemoryResult::Ok) return false;
        return imports_.register_export({std::move(library),ordinal,guest_address,
                                         ImportKind::Function});
    }
    [[nodiscard]] ImportResolveError resolve_imports(std::span<const XexImport> imports,
        memory::PhysicalMemory& ram) const { return imports_.resolve(imports,ram); }

private:
    std::map<std::uint16_t,Handler> services_;
    XexImportResolver imports_;
};
} // namespace wx360::kernel
