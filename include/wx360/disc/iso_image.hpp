#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>

namespace wx360::disc {

class IsoImage {
public:
    static constexpr std::uint32_t kSectorSize = 2048;
    static constexpr std::uint32_t kVolumeDescriptorSector = 32;
    static constexpr std::string_view kXdvdfsMagic = "MICROSOFT*XBOX*MEDIA";

    bool open(const std::string& path) {
        close();
        file_.open(path, std::ios::binary);
        if (!file_) return fail("unable to open ISO image");
        file_.seekg(0, std::ios::end);
        const auto end = file_.tellg();
        if (end < 0 || static_cast<std::uint64_t>(end) < (kVolumeDescriptorSector + 1ULL) * kSectorSize)
            return fail("image is too small for an Xbox 360 volume descriptor");
        image_size_ = static_cast<std::uint64_t>(end);
        std::uint8_t descriptor[kSectorSize]{};
        if (!read_sector(kVolumeDescriptorSector, descriptor, sizeof(descriptor)))
            return fail("unable to read volume descriptor");
        if (!matches_magic(descriptor, sizeof(descriptor)))
            return fail("Xbox 360 XDVDFS volume descriptor not found");
        root_sector_ = read_le32(descriptor + 20);
        root_size_ = read_le32(descriptor + 24);
        if (root_size_ == 0 || !range_valid(root_sector_, root_size_))
            return fail("invalid XDVDFS root directory extent");
        error_.clear();
        return true;
    }

    void close() noexcept {
        if (file_.is_open()) file_.close();
        image_size_ = 0;
        root_sector_ = root_size_ = 0;
        error_.clear();
    }

    [[nodiscard]] bool is_open() const noexcept { return file_.is_open() && error_.empty(); }
    [[nodiscard]] std::uint32_t root_sector() const noexcept { return root_sector_; }
    [[nodiscard]] std::uint32_t root_size() const noexcept { return root_size_; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }

    bool read_sector(std::uint32_t sector, std::uint8_t* output, std::size_t size) {
        if (!file_.is_open() || output == nullptr || size != kSectorSize) return false;
        const std::uint64_t offset = static_cast<std::uint64_t>(sector) * kSectorSize;
        if (offset > image_size_ || size > image_size_ - offset) return false;
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        file_.read(reinterpret_cast<char*>(output), static_cast<std::streamsize>(size));
        return file_.gcount() == static_cast<std::streamsize>(size);
    }

private:
    static std::uint32_t read_le32(const std::uint8_t* p) noexcept {
        return static_cast<std::uint32_t>(p[0]) |
               (static_cast<std::uint32_t>(p[1]) << 8) |
               (static_cast<std::uint32_t>(p[2]) << 16) |
               (static_cast<std::uint32_t>(p[3]) << 24);
    }

    static bool matches_magic(const std::uint8_t* data, std::size_t size) noexcept {
        if (size < kXdvdfsMagic.size()) return false;
        for (std::size_t i = 0; i < kXdvdfsMagic.size(); ++i)
            if (data[i] != static_cast<std::uint8_t>(kXdvdfsMagic[i])) return false;
        return true;
    }

    bool range_valid(std::uint32_t sector, std::uint32_t bytes) const noexcept {
        const std::uint64_t offset = static_cast<std::uint64_t>(sector) * kSectorSize;
        return offset <= image_size_ && bytes <= image_size_ - offset;
    }

    bool fail(const char* message) {
        error_ = message;
        if (file_.is_open()) file_.close();
        return false;
    }

    std::ifstream file_;
    std::uint64_t image_size_{0};
    std::uint32_t root_sector_{0};
    std::uint32_t root_size_{0};
    std::string error_;
};

} // namespace wx360::disc
