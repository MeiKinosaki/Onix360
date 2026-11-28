#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace onix360::memory {

enum class Access : std::uint8_t { Read = 1, Write = 2, Execute = 4 };

enum class MemoryResult : std::uint8_t {
    Ok,
    Unmapped,
    PermissionDenied,
    OutOfRange,
    AlignmentFault,
};

struct Fault {
    std::uint64_t address{};
    Access access{Access::Read};
    MemoryResult reason{MemoryResult::Ok};
};

class PhysicalMemory {
public:
    static constexpr std::size_t page_size = 4096;

    explicit PhysicalMemory(std::size_t size_bytes)
        : bytes_(size_bytes, 0), page_permissions_(
              (size_bytes + page_size - 1U) / page_size,
              static_cast<std::uint8_t>(Access::Read) |
              static_cast<std::uint8_t>(Access::Write) |
              static_cast<std::uint8_t>(Access::Execute)) {}

    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
    [[nodiscard]] std::span<std::uint8_t> raw_bytes() noexcept { return bytes_; }
    [[nodiscard]] std::span<const std::uint8_t> raw_bytes() const noexcept { return bytes_; }

    [[nodiscard]] MemoryResult set_page_permissions(
        std::uint64_t address, std::size_t length, std::uint8_t permissions) noexcept {
        if (length == 0 || address >= bytes_.size() ||
            length > bytes_.size() - static_cast<std::size_t>(address))
            return MemoryResult::OutOfRange;
        const std::size_t first = static_cast<std::size_t>(address) / page_size;
        const std::size_t last = (static_cast<std::size_t>(address) + length - 1U) / page_size;
        for (std::size_t page = first; page <= last; ++page)
            page_permissions_[page] = permissions & 0x7U;
        return MemoryResult::Ok;
    }

    [[nodiscard]] std::optional<std::uint8_t> page_permissions(
        std::uint64_t address) const noexcept {
        if (address >= bytes_.size()) return std::nullopt;
        return page_permissions_[static_cast<std::size_t>(address) / page_size];
    }

    [[nodiscard]] MemoryResult check_access(
        std::uint64_t address, std::size_t length, Access access) const noexcept {
        return validate(address, length, access);
    }

    [[nodiscard]] MemoryResult read(
        std::uint64_t address, std::span<std::uint8_t> output,
        Access access = Access::Read) const noexcept {
        const auto check = validate(address, output.size(), access);
        if (check != MemoryResult::Ok) return check;
        for (std::size_t i = 0; i < output.size(); ++i)
            output[i] = bytes_[static_cast<std::size_t>(address) + i];
        return MemoryResult::Ok;
    }

    [[nodiscard]] MemoryResult write(
        std::uint64_t address, std::span<const std::uint8_t> input) noexcept {
        const auto check = validate(address, input.size(), Access::Write);
        if (check != MemoryResult::Ok) return check;
        for (std::size_t i = 0; i < input.size(); ++i)
            bytes_[static_cast<std::size_t>(address) + i] = input[i];
        return MemoryResult::Ok;
    }

    [[nodiscard]] MemoryResult fetch_u32(
        std::uint64_t address, std::uint32_t& value) const noexcept {
        if ((address & 0x3U) != 0) return MemoryResult::AlignmentFault;
        std::uint8_t bytes[4]{};
        const auto status = read(address, bytes, Access::Execute);
        if (status != MemoryResult::Ok) return status;
        value = (static_cast<std::uint32_t>(bytes[0]) << 24U) |
                (static_cast<std::uint32_t>(bytes[1]) << 16U) |
                (static_cast<std::uint32_t>(bytes[2]) << 8U) |
                static_cast<std::uint32_t>(bytes[3]);
        return MemoryResult::Ok;
    }

private:
    [[nodiscard]] MemoryResult validate(
        std::uint64_t address, std::size_t length, Access access) const noexcept {
        if (length == 0) return MemoryResult::Ok;
        if (address > bytes_.size() ||
            length > bytes_.size() - static_cast<std::size_t>(address))
            return MemoryResult::OutOfRange;
        const auto required = static_cast<std::uint8_t>(access);
        const std::size_t first = static_cast<std::size_t>(address) / page_size;
        const std::size_t last = (static_cast<std::size_t>(address) + length - 1U) / page_size;
        for (std::size_t page = first; page <= last; ++page) {
            if ((page_permissions_[page] & required) == 0)
                return MemoryResult::PermissionDenied;
        }
        return MemoryResult::Ok;
    }

    std::vector<std::uint8_t> bytes_;
    std::vector<std::uint8_t> page_permissions_;
};

} // namespace onix360::memory
