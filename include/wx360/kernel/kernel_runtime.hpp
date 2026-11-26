#pragma once
#include "wx360/cpu/xenon_state.hpp"
#include "wx360/kernel/xex_import_resolver.hpp"
#include "wx360/kernel/xbox360_export_metadata.hpp"
#include "wx360/memory/physical_memory.hpp"
#include "wx360/memory/address_space.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string_view>
#include <vector>
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
    // Register a host implementation and its guest-callable export as one
    // operation. If trampoline creation fails, roll back the service entry so
    // the runtime cannot advertise an export that has no dispatch target.
    [[nodiscard]] bool register_function_export(
        std::string library, std::uint16_t ordinal, std::uint16_t service_id,
        std::uint32_t guest_address, Handler handler,
        memory::PhysicalMemory& ram) {
        if (!handler || service_id > 0x7FFFU ||
            !register_service(service_id, handler))
            return false;
        const auto key = make_export_key(library, ordinal);
        if (emit_trampoline(library, ordinal, service_id, guest_address, ram)) {
            export_services_.emplace(key, service_id);
            return true;
        }
        services_.erase(service_id);
        return false;
    }

    // Bind a caller-supplied, implemented host handler to an ordinal from
    // the source-verified metadata subset. Metadata alone never creates a
    // callable export; the handler and guest trampoline are both required.
    [[nodiscard]] bool register_known_function_export(
        std::string_view library, std::uint16_t ordinal,
        std::uint16_t service_id, std::uint32_t guest_address,
        Handler handler, memory::PhysicalMemory& ram) {
        const auto* metadata = find_export_metadata(library, ordinal);
        if (!metadata || metadata->kind != ExportMetadataKind::Function ||
            !handler)
            return false;
        return register_function_export(std::string(metadata->library), ordinal,
            service_id, guest_address, handler, ram);
    }

    [[nodiscard]] bool emit_trampoline(std::string library, std::uint16_t ordinal,
        std::uint16_t service_id, std::uint32_t guest_address,
        memory::PhysicalMemory& ram) {
        if (service_id > 0x7FFFU || guest_address == 0 ||
            imports_.has_export(library, ordinal) ||
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
    [[nodiscard]] bool dispatch_import_thunk(
        std::uint64_t guest_pc, cpu::XenonThreadState& state) const noexcept {
        const auto thunk = thunk_services_.find(static_cast<std::uint32_t>(guest_pc));
        if (thunk == thunk_services_.end()) return false;
        const auto service = services_.find(thunk->second);
        if (service == services_.end()) return false;
        service->second(state);
        return true;
    }

    // Resolve guest-virtual XEX imports as one transaction. Validate every
    // registered service and executable thunk mapping before the resolver
    // writes any guest bytes; stage dispatch entries before committing patches.
    [[nodiscard]] ImportResolveError resolve_imports_virtual(
        std::span<const XexImport> imports,
        const memory::AddressSpace& address_space,
        memory::PhysicalMemory& ram) {
        auto staged_thunks = thunk_services_;
        for (const auto& item : imports) {
            if (item.kind != ImportKind::Function) continue;
            const auto service = export_services_.find(
                make_export_key(item.library, item.ordinal));
            if (service == export_services_.end())
                return ImportResolveError::MissingExport;
            const auto executable = address_space.translate_range(
                item.thunk_address, 16, memory::Access::Execute, ram);
            if (executable.result != memory::MemoryResult::Ok)
                return ImportResolveError::TranslationFailed;
            const auto key = static_cast<std::uint32_t>(item.thunk_address);
            if (!staged_thunks.emplace(key, service->second).second)
                return ImportResolveError::DuplicateThunk;
        }
        const auto result = imports_.resolve_virtual(imports, address_space, ram);
        if (result != ImportResolveError::None) return result;
        thunk_services_.swap(staged_thunks);
        return ImportResolveError::None;
    }

    [[nodiscard]] ImportResolveError resolve_imports(
        std::span<const XexImport> imports, memory::PhysicalMemory& ram) {
        // Do not patch guest code unless every function import has a real
        // registered host implementation that can be dispatched by thunk PC.
        for (const auto& item : imports) {
            if (item.kind == ImportKind::Function &&
                export_services_.find(make_export_key(item.library, item.ordinal)) ==
                    export_services_.end())
                return ImportResolveError::MissingExport;
        }
        const auto result = imports_.resolve(imports, ram);
        if (result != ImportResolveError::None) return result;
        for (const auto& item : imports) {
            if (item.kind == ImportKind::Function) {
                const auto service = export_services_.find(
                    make_export_key(item.library, item.ordinal));
                if (service != export_services_.end())
                    thunk_services_[static_cast<std::uint32_t>(item.thunk_address)] =
                        service->second;
            }
        }
        return ImportResolveError::None;
    }

private:
    [[nodiscard]] static std::string make_export_key(
        std::string_view library, std::uint16_t ordinal) {
        std::string key;
        key.reserve(library.size() + 8);
        for (const unsigned char ch : library)
            key.push_back(static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch));
        key.push_back('#');
        key.push_back(static_cast<char>(ordinal >> 8));
        key.push_back(static_cast<char>(ordinal));
        return key;
    }

    std::map<std::uint16_t,Handler> services_;
    std::map<std::string,std::uint16_t> export_services_;
    std::map<std::uint32_t,std::uint16_t> thunk_services_;
    XexImportResolver imports_;
};
} // namespace wx360::kernel
