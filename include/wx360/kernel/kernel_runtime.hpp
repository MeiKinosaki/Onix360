#pragma once
#include "wx360/cpu/xenon_state.hpp"
#include "wx360/kernel/xex_import_resolver.hpp"
#include "wx360/kernel/kernel_heap.hpp"
#include "wx360/kernel/kernel_thread_scheduler.hpp"
#include "wx360/kernel/kernel_synchronization.hpp"
#include "wx360/storage/guest_file_system.hpp"
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
        return dispatch_service(id, state);
    }

    // Cooperative guest-thread lifecycle; scheduling occurs at explicit
    // guest yield/block/termination boundaries, never on arbitrary host timing.
    [[nodiscard]] std::optional<std::uint32_t> create_guest_thread(
        const cpu::XenonThreadState& initial, std::uint8_t priority = 16) {
        return threads_.create(initial, priority);
    }
    [[nodiscard]] bool schedule_guest_thread(cpu::XenonThreadState& live) noexcept {
        return threads_.schedule(live);
    }
    [[nodiscard]] bool yield_guest_thread(cpu::XenonThreadState& live) noexcept {
        return threads_.yield(live);
    }
    [[nodiscard]] bool block_guest_thread(cpu::XenonThreadState& live) noexcept {
        return threads_.block_current(live);
    }
    [[nodiscard]] bool unblock_guest_thread(std::uint32_t id) noexcept {
        return threads_.unblock(id);
    }
    [[nodiscard]] bool terminate_guest_thread(cpu::XenonThreadState& live) noexcept {
        const auto current = threads_.current_id();
        if (!current) return false;
        synchronization_.cancel_thread(*current);
        return threads_.terminate_current(live);
    }
    struct PoolTagStatistics {
        std::uint32_t tag{};
        std::size_t live_allocations{};
        std::uint64_t live_bytes{};
    };
    [[nodiscard]] PoolTagStatistics pool_tag_statistics(std::uint32_t tag) const noexcept {
        PoolTagStatistics stats{};
        stats.tag = tag;
        for (const auto& [address, allocation] : tagged_allocations_) {
            (void)address;
            if (allocation.second == tag) {
                ++stats.live_allocations;
                stats.live_bytes += allocation.first;
            }
        }
        return stats;
    }

    [[nodiscard]] const KernelThreadScheduler& thread_scheduler() const noexcept {
        return threads_;
    }

    [[nodiscard]] std::optional<std::uint32_t> create_guest_event(
        bool manual_reset, bool initially_signaled = false) {
        return synchronization_.create_event(manual_reset, initially_signaled);
    }
    [[nodiscard]] std::optional<std::uint32_t> create_guest_semaphore(
        std::uint32_t initial_count, std::uint32_t maximum_count) {
        return synchronization_.create_semaphore(initial_count, maximum_count);
    }
    [[nodiscard]] WaitResult wait_guest_event(
        std::uint32_t event_id, cpu::XenonThreadState& live) {
        const auto current = threads_.current_id();
        if (!current) return WaitResult::InvalidObject;
        const auto result = synchronization_.wait_event(event_id, *current);
        if (result == WaitResult::Blocked)
            (void)threads_.block_current(live);
        return result;
    }
    [[nodiscard]] bool set_guest_event(std::uint32_t event_id) {
        if (!synchronization_.has_event(event_id)) return false;
        const auto released = synchronization_.set_event(event_id);
        bool ok = true;
        for (const auto id : released) ok = threads_.unblock(id) && ok;
        return ok;
    }
    [[nodiscard]] bool reset_guest_event(std::uint32_t event_id) noexcept {
        return synchronization_.reset_event(event_id);
    }
    [[nodiscard]] WaitResult wait_guest_semaphore(
        std::uint32_t semaphore_id, cpu::XenonThreadState& live) {
        const auto current = threads_.current_id();
        if (!current) return WaitResult::InvalidObject;
        const auto result = synchronization_.wait_semaphore(semaphore_id, *current);
        if (result == WaitResult::Blocked)
            (void)threads_.block_current(live);
        return result;
    }
    [[nodiscard]] bool release_guest_semaphore(
        std::uint32_t semaphore_id, std::uint32_t count = 1) {
        const auto released = synchronization_.release_semaphore(semaphore_id, count);
        if (!released) return false;
        bool ok = true;
        for (const auto id : *released) ok = threads_.unblock(id) && ok;
        return ok;
    }

    // Frontends mount verified game/content files; guest code receives only
    // bounded handles and cannot address arbitrary host filesystem paths.
    [[nodiscard]] storage::GuestFileSystem& guest_file_system() noexcept {
        return guest_files_;
    }
    [[nodiscard]] const storage::GuestFileSystem& guest_file_system() const noexcept {
        return guest_files_;
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
        return dispatch_service(thunk->second, state);
    }

    // Register the source-listed pool exports against a bounded guest heap.
    // Export ordinals are metadata-checked; service IDs are emulator-internal.
    [[nodiscard]] bool register_kernel_heap_exports(
        KernelHeap& heap, std::uint16_t allocate_service,
        std::uint16_t free_service, std::uint32_t allocate_thunk,
        std::uint32_t free_thunk, memory::PhysicalMemory& ram) {
        if (!heap.valid() || allocate_service == free_service || heap_ ||
            allocate_service > 0x7FFFU || free_service > 0x7FFFU ||
            services_.contains(allocate_service) || services_.contains(free_service) ||
            imports_.has_export("xboxkrnl.exe", 0x0009) ||
            imports_.has_export("xboxkrnl.exe", 0x000F) ||
            ram.check_access(allocate_thunk, 12, memory::Access::Write) != memory::MemoryResult::Ok ||
            ram.check_access(free_thunk, 12, memory::Access::Write) != memory::MemoryResult::Ok)
            return false;
        heap_ = &heap;
        allocate_pool_service_ = allocate_service;
        free_pool_service_ = free_service;
        const auto noop = +[](cpu::XenonThreadState&) noexcept {};
        if (!register_known_function_export("xboxkrnl.exe", 0x0009,
                allocate_service, allocate_thunk, noop, ram) ||
            !register_known_function_export("xboxkrnl.exe", 0x000F,
                free_service, free_thunk, noop, ram)) {
            heap_ = nullptr;
            services_.erase(allocate_service);
            services_.erase(free_service);
            export_services_.erase(make_export_key("xboxkrnl.exe", 0x0009));
            export_services_.erase(make_export_key("xboxkrnl.exe", 0x000F));
            return false;
        }
        return true;
    }

    // Register ExAllocatePoolWithTag as a distinct source-listed export.
    // The tag is accepted per the guest ABI; allocation accounting remains
    // address/size based until per-tag diagnostics are implemented.
    [[nodiscard]] bool register_kernel_heap_tagged_export(
        KernelHeap& heap, std::uint16_t service_id,
        std::uint32_t guest_thunk, memory::PhysicalMemory& ram) {
        if (!heap.valid() || heap_ != &heap || service_id > 0x7FFFU ||
            services_.contains(service_id) ||
            imports_.has_export("xboxkrnl.exe", 0x000A) ||
            ram.check_access(guest_thunk, 12, memory::Access::Write) != memory::MemoryResult::Ok)
            return false;
        tagged_allocate_pool_service_ = service_id;
        if (!register_known_function_export("xboxkrnl.exe", 0x000A,
                service_id, guest_thunk,
                +[](cpu::XenonThreadState&) noexcept {}, ram)) {
            tagged_allocate_pool_service_.reset();
            return false;
        }
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
    [[nodiscard]] bool dispatch_service(std::uint16_t id,
        cpu::XenonThreadState& state) const noexcept {
        if (heap_ && id == allocate_pool_service_) {
            const auto pool_type = state.gpr[3];
            const auto bytes = state.gpr[4];
            if (pool_type > 1U || bytes > UINT32_MAX) {
                state.gpr[3] = 0;
                return true;
            }
            const auto result = heap_->allocate(static_cast<std::uint32_t>(bytes), 16);
            state.gpr[3] = result ? *result : 0U;
            return true;
        }
        if (heap_ && tagged_allocate_pool_service_ &&
            id == *tagged_allocate_pool_service_) {
            const auto pool_type = state.gpr[3];
            const auto bytes = state.gpr[4];
            // r5 carries the caller's pool tag. It is currently accepted but
            // not retained; unsupported pool types fail safely with NULL.
            if (pool_type > 1U || bytes == 0 || bytes > UINT32_MAX) {
                state.gpr[3] = 0;
                return true;
            }
            const auto result = heap_->allocate(static_cast<std::uint32_t>(bytes), 16);
            if (result) tagged_allocations_.emplace(*result,
                std::pair<std::uint32_t,std::uint32_t>{static_cast<std::uint32_t>(bytes), state.gpr[5]});
            state.gpr[3] = result ? *result : 0U;
            return true;
        }
        if (heap_ && id == free_pool_service_) {
            const auto address = static_cast<std::uint32_t>(state.gpr[3]);
            if (heap_->release(address)) tagged_allocations_.erase(address);
            return true;
        }
        const auto it = services_.find(id);
        if (it == services_.end()) return false;
        it->second(state);
        return true;
    }

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

    KernelHeap* heap_{};
    KernelThreadScheduler threads_{};
    KernelSynchronization synchronization_{};
    storage::GuestFileSystem guest_files_{};
    std::uint16_t allocate_pool_service_{};
    std::uint16_t free_pool_service_{};
    std::optional<std::uint16_t> tagged_allocate_pool_service_{};
    // address -> (allocation size, caller-supplied pool tag)
    mutable std::map<std::uint32_t,std::pair<std::uint32_t,std::uint32_t>> tagged_allocations_;
    std::map<std::uint16_t,Handler> services_;
    std::map<std::string,std::uint16_t> export_services_;
    std::map<std::uint32_t,std::uint16_t> thunk_services_;
    XexImportResolver imports_;
};
} // namespace wx360::kernel
