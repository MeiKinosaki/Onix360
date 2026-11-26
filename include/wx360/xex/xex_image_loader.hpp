#pragma once

#include "wx360/xex/xex_header.hpp"
#include "wx360/xex/xex_security_info.hpp"
#include "wx360/xex/xex_import_table.hpp"
#include "wx360/crypto/aes128.hpp"
#include "wx360/xex/xex_pe_mapper.hpp"
#include "wx360/kernel/xex_import_resolver.hpp"
#include "wx360/kernel/kernel_runtime.hpp"
#include "wx360/compression/lzx_decoder.hpp"
#include <bit>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <utility>
#include <vector>

namespace wx360::xex {

enum class LoadError : std::uint8_t {
    None,
    CannotOpen,
    InvalidFileSize,
    HeaderReadFailure,
    InvalidHeader,
    ImageNotMapped,
    TransformIncomplete,
    PeMappingFailed,
    SecurityInfoInvalid,
    FileFormatInfoInvalid,
    ImportTableInvalid,
};

struct LaunchPlan {
    Header header{};
    std::filesystem::path source;
    std::uint64_t file_size{};
    std::uint32_t entry_point{}; // XEX virtual entry point; inspection only
};

struct FullXexImage {
    std::vector<std::uint8_t> bytes;
    Header header{};
    SecurityInfo security{};
    FileFormatInfo format{};
    ImportTable imports{};
};

class XexImageLoader;

// Constructible only by the production loader after metadata, page hashes,
// transforms and PE mapping have succeeded. There are no caller-set success flags.
class ValidatedXexImage {
public:
    [[nodiscard]] std::uint64_t entry_point() const noexcept { return mapped_entry_point_; }
    [[nodiscard]] std::uint64_t guest_entry_point() const noexcept { return guest_entry_point_; }
    [[nodiscard]] std::span<const memory::AddressSpace::PageMapping> page_mappings() const noexcept {
        return page_mappings_;
    }
private:
    explicit ValidatedXexImage(std::uint64_t entry, std::uint64_t guest_entry,
        std::vector<memory::AddressSpace::PageMapping> mappings)
        : mapped_entry_point_(entry), guest_entry_point_(guest_entry),
          page_mappings_(std::move(mappings)) {}
    std::uint64_t mapped_entry_point_{};
    std::uint64_t guest_entry_point_{};
    std::vector<memory::AddressSpace::PageMapping> page_mappings_{};
    friend class XexImageLoader;
};

struct LoadResult {
    LoadError error{LoadError::None};
    LaunchPlan plan{};
    [[nodiscard]] explicit operator bool() const noexcept {
        return error == LoadError::None;
    }
};

enum class NormalDecompressError : std::uint8_t {
    Ok, WrongCompression, InvalidWindow, InvalidBlockStream, LzxFailure
};

enum class NormalTransformError : std::uint8_t {
    Ok, WrongCompression, InvalidImageSize, KeyUnavailable, DecryptionFailed, DecompressionFailed
};

class XexImageLoader {
public:
    static constexpr std::size_t kHeaderWindow = 0x10000;
    static constexpr std::uint64_t kMaximumImageSize = 2ull * 1024ull * 1024ull * 1024ull;

    // Input must already be decrypted. XEX Normal consists of linked
    // SHA-1-authenticated blocks; each block contains u16-length-prefixed
    // LZX chunks. Strip and authenticate that framing before LZX decoding.
    [[nodiscard]] static NormalDecompressError decompress_normal_lzx(
        std::span<const std::uint8_t> decrypted_payload,
        const FileFormatInfo& format,
        std::vector<std::uint8_t>& output) {
        if (format.compression != XexCompression::Normal)
            return NormalDecompressError::WrongCompression;
        const auto window = format.normal_window_size;
        if (window < 0x8000u || window > 0x200000u ||
            (window & (window - 1u)) != 0)
            return NormalDecompressError::InvalidWindow;

        std::vector<std::uint8_t> compressed;
        const auto framing = unpack_normal_chunks(
            decrypted_payload, format.first_normal_block,
            decrypted_payload.size(), compressed, false);
        if (framing != NormalBlockError::Ok)
            return NormalDecompressError::InvalidBlockStream;

        std::vector<std::uint8_t> decoded;
        const auto result = compression::decode_lzx(
            compressed, std::countr_zero(window), 0, format.image_size, decoded);
        if (result != compression::LzxError::None)
            return NormalDecompressError::LzxFailure;
        output = std::move(decoded);
        return NormalDecompressError::Ok;
    }

    [[nodiscard]] static kernel::ImportResolveError resolve_kernel_imports(
        std::span<const kernel::XexImport> imports,
        const kernel::XexImportResolver& resolver,
        memory::PhysicalMemory& guest_memory) {
        return resolver.resolve(imports, guest_memory);
    }

    // Single production launch API. The loader selects the transform from
    // parsed XEX metadata, verifies the page-hash chain, maps PE sections and
    // returns an opaque token only after every required stage succeeds.
    // RSA signature verification and import-library resolution remain separate
    // trust/loader stages and are not implied by this token.
    [[nodiscard]] static std::optional<ValidatedXexImage> load_validated(
        const FullXexImage& image,
        memory::PhysicalMemory& guest_memory,
        std::uint64_t physical_base,
        std::uint32_t guest_image_base,
        crypto::XexKeySet key_set = crypto::XexKeySet::Retail,
        kernel::KernelRuntime* kernel_runtime = nullptr,
        memory::AddressSpace* guest_address_space = nullptr) {
        if ((image.header.has_import_libraries &&
             (!kernel_runtime || !guest_address_space)) ||
            image.security.image_size != image.format.image_size ||
            image.header.entry_point == 0)
            return std::nullopt;

        std::vector<std::uint8_t> transformed;
        if (image.format.compression == XexCompression::None &&
            image.format.encryption == XexEncryption::None) {
            const auto offset = static_cast<std::size_t>(image.header.pe_data_offset);
            const auto length = static_cast<std::size_t>(image.format.image_size);
            if (offset > image.bytes.size() || length > image.bytes.size() - offset)
                return std::nullopt;
            const auto pe = std::span<const std::uint8_t>(image.bytes).subspan(offset, length);
            if (verify_page_hashes(pe, image.security) != PageHashResult::Ok)
                return std::nullopt;
            transformed.assign(pe.begin(), pe.end());
        } else if (image.format.compression == XexCompression::Normal) {
            const auto offset = static_cast<std::size_t>(image.header.pe_data_offset);
            if (offset > image.bytes.size()) return std::nullopt;
            const auto payload = std::span<const std::uint8_t>(image.bytes).subspan(offset);
            if (transform_normal_lzx_verified(
                    image.security, key_set, image.format, payload, transformed) !=
                NormalTransformError::Ok)
                return std::nullopt;
        } else {
            return std::nullopt;
        }

        if (transformed.size() != image.format.image_size)
            return std::nullopt;
        const auto mapped = XexPeMapper::map(
            transformed, guest_memory, physical_base, guest_image_base,
            image.header.entry_point);
        if (!mapped) return std::nullopt;

        // Stage the image's virtual mappings in a private AddressSpace copy.
        // The caller-visible MMU is committed only after imports resolve.
        if (image.header.has_import_libraries) {
            memory::AddressSpace staged_address_space;
            try {
                staged_address_space = *guest_address_space;
            } catch (...) {
                return std::nullopt;
            }
            if (staged_address_space.map_pages(mapped.page_mappings) !=
                memory::MemoryResult::Ok)
                return std::nullopt;

            std::vector<kernel::XexImport> decoded_imports;
            if (decode_import_records_virtual(image.imports, staged_address_space,
                    guest_memory, decoded_imports) != ImportTableError::Ok)
                return std::nullopt;

            // Keep PE permissions intact except while the import transaction
            // writes its descriptor and function-thunk patches.
            std::vector<std::pair<std::uint64_t, std::uint8_t>> restore;
            const auto grant_write = [&](std::uint64_t va, std::size_t length) -> bool {
                const auto translated = staged_address_space.translate_range(
                    va, length, memory::Access::Read, guest_memory);
                if (translated.result != memory::MemoryResult::Ok) return false;
                const auto first = translated.physical_address / memory::PhysicalMemory::page_size;
                const auto last = (translated.physical_address + length - 1U) /
                                  memory::PhysicalMemory::page_size;
                for (auto page = first; page <= last; ++page) {
                    const auto pa = page * memory::PhysicalMemory::page_size;
                    const auto prior = guest_memory.page_permissions(pa);
                    if (!prior) return false;
                    if (std::none_of(restore.begin(), restore.end(),
                        [&](const auto& entry) { return entry.first == pa; }))
                        restore.emplace_back(pa, *prior);
                    if ((*prior & static_cast<std::uint8_t>(memory::Access::Write)) == 0 &&
                        guest_memory.set_page_permissions(pa, memory::PhysicalMemory::page_size,
                            static_cast<std::uint8_t>(*prior |
                                static_cast<std::uint8_t>(memory::Access::Write))) !=
                            memory::MemoryResult::Ok)
                        return false;
                }
                return true;
            };
            bool ready = true;
            for (const auto& item : decoded_imports) {
                if (item.kind == kernel::ImportKind::Function &&
                    !grant_write(item.thunk_address, 16)) { ready = false; break; }
                if (!grant_write(item.value_address, 4)) { ready = false; break; }
            }
            const auto resolved = ready
                ? kernel_runtime->resolve_imports_virtual(decoded_imports,
                    staged_address_space, guest_memory)
                : kernel::ImportResolveError::TranslationFailed;
            bool restored = true;
            for (auto it = restore.rbegin(); it != restore.rend(); ++it)
                if (guest_memory.set_page_permissions(it->first,
                    memory::PhysicalMemory::page_size, it->second) != memory::MemoryResult::Ok)
                    restored = false;
            if (resolved != kernel::ImportResolveError::None || !restored)
                return std::nullopt;
            *guest_address_space = std::move(staged_address_space);
        }
        return ValidatedXexImage(mapped.mapped_entry,
                                 static_cast<std::uint64_t>(mapped.image_base) + mapped.entry_rva,
                                 mapped.page_mappings);
    }

    [[nodiscard]] static crypto::AesResult decrypt_normal_payload(
        const SecurityInfo& security, crypto::XexKeySet key_set,
        const crypto::Aes128Key& cbc_iv,
        std::span<const std::uint8_t> encrypted_payload,
        std::vector<std::uint8_t>& decrypted_payload) {
        if (!crypto::xex_master_key_available(key_set))
            return crypto::AesResult::KeyUnavailable;
        const auto session_key = crypto::unwrap_xex_session_key(
            key_set, security.encrypted_image_key);
        return crypto::aes128_cbc_decrypt(
            session_key, cbc_iv, encrypted_payload, decrypted_payload);
    }

    [[nodiscard]] static crypto::AesResult decrypt_xex_normal_payload(
        const SecurityInfo& security, crypto::XexKeySet key_set,
        std::span<const std::uint8_t> encrypted_payload,
        std::vector<std::uint8_t>& decrypted_payload) {
        if (!crypto::xex_master_key_available(key_set))
            return crypto::AesResult::KeyUnavailable;
        const auto session_key = crypto::unwrap_xex_session_key(
            key_set, security.encrypted_image_key);
        return crypto::aes128_xex_cbc_decrypt(
            session_key, encrypted_payload, decrypted_payload);
    }

    // End-to-end Normal path: unwrap the per-image key, decrypt the complete
    // payload when encryption is NORMAL, authenticate/deblock, then LZX decode.
    // Output is committed only after every stage succeeds.
    [[nodiscard]] static NormalTransformError transform_normal_lzx_payload(
        const SecurityInfo& security, crypto::XexKeySet key_set,
        const FileFormatInfo& format,
        std::span<const std::uint8_t> payload,
        std::vector<std::uint8_t>& output) {
        if (format.compression != XexCompression::Normal)
            return NormalTransformError::WrongCompression;
        if (security.image_size != 0 && security.image_size != format.image_size)
            return NormalTransformError::InvalidImageSize;

        std::vector<std::uint8_t> decrypted;
        if (format.encryption == XexEncryption::Normal) {
            const auto decrypt_result = decrypt_xex_normal_payload(
                security, key_set, payload, decrypted);
            if (decrypt_result == crypto::AesResult::KeyUnavailable)
                return NormalTransformError::KeyUnavailable;
            if (decrypt_result != crypto::AesResult::Ok)
                return NormalTransformError::DecryptionFailed;
        } else if (format.encryption == XexEncryption::None) {
            decrypted.assign(payload.begin(), payload.end());
        } else {
            return NormalTransformError::DecryptionFailed;
        }

        std::vector<std::uint8_t> decoded;
        if (decompress_normal_lzx(decrypted, format, decoded) != NormalDecompressError::Ok)
            return NormalTransformError::DecompressionFailed;
        output = std::move(decoded);
        return NormalTransformError::Ok;
    }

    // Reads the complete bounded XEX2 file and parses both SecurityInfo
    // and FileFormatInfo. This is metadata acquisition only: it does not
    // claim RSA signature verification or payload decryption.
    [[nodiscard]] static LoadError read_complete(
        const std::filesystem::path& path, FullXexImage& output) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return LoadError::CannotOpen;
        file.seekg(0, std::ios::end);
        const auto end = file.tellg();
        if (end < 24 || static_cast<std::uint64_t>(end) > kMaximumImageSize)
            return LoadError::InvalidFileSize;
        FullXexImage parsed{};
        parsed.bytes.resize(static_cast<std::size_t>(end));
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(parsed.bytes.data()),
                  static_cast<std::streamsize>(parsed.bytes.size()));
        if (static_cast<std::size_t>(file.gcount()) != parsed.bytes.size())
            return LoadError::HeaderReadFailure;
        if (parse_header(parsed.bytes, parsed.header, parsed.bytes.size()) != ParseResult::Ok)
            return LoadError::InvalidHeader;
        if (parse_security_info(parsed.bytes, parsed.header.security_info_offset,
                                parsed.security) != SecurityParseError::Ok)
            return LoadError::SecurityInfoInvalid;
        if (parse_file_format_info(parsed.bytes, parsed.header, parsed.format) !=
            FormatParseResult::Ok)
            return LoadError::FileFormatInfoInvalid;
        if (parsed.security.image_size != parsed.format.image_size)
            return LoadError::SecurityInfoInvalid;
        if (parsed.header.has_import_libraries &&
            parse_import_table(parsed.bytes, parsed.header, parsed.imports) !=
                ImportTableError::Ok)
            return LoadError::ImportTableInvalid;
        output = std::move(parsed);
        return LoadError::None;
    }

    // Normal/LZX transformation followed by mandatory page-hash verification.
    // No output is exposed unless both the block chain and image page digests pass.
    [[nodiscard]] static NormalTransformError transform_normal_lzx_verified(
        const SecurityInfo& security, crypto::XexKeySet key_set,
        const FileFormatInfo& format,
        std::span<const std::uint8_t> payload,
        std::vector<std::uint8_t>& output) {
        std::vector<std::uint8_t> transformed;
        const auto status = transform_normal_lzx_payload(
            security, key_set, format, payload, transformed);
        if (status != NormalTransformError::Ok) return status;
        if (verify_page_hashes(transformed, security) != PageHashResult::Ok)
            return NormalTransformError::DecompressionFailed;
        output = std::move(transformed);
        return NormalTransformError::Ok;
    }

    // Inspects XEX2 metadata and prepares a launch plan. It deliberately does
    // not claim to decrypt, decompress, map PE sections or resolve imports.
    [[nodiscard]] static LoadResult inspect(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return {LoadError::CannotOpen, {}};
        file.seekg(0, std::ios::end);
        const auto end = file.tellg();
        if (end < 24) return {LoadError::InvalidFileSize, {}};
        const auto size = static_cast<std::uint64_t>(end);
        if (size > kMaximumImageSize) return {LoadError::InvalidFileSize, {}};
        const auto window_size = static_cast<std::size_t>(
            std::min<std::uint64_t>(size, kHeaderWindow));
        std::array<std::uint8_t, kHeaderWindow> window{};
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(window.data()),
                  static_cast<std::streamsize>(window_size));
        if (static_cast<std::size_t>(file.gcount()) != window_size)
            return {LoadError::HeaderReadFailure, {}};
        Header header{};
        if (parse_header(std::span<const std::uint8_t>(window.data(), window_size),
                         header, size) != ParseResult::Ok)
            return {LoadError::InvalidHeader, {}};
        LaunchPlan plan{};
        plan.header = header;
        plan.source = path;
        plan.file_size = size;
        plan.entry_point = header.entry_point;
        return {LoadError::None, std::move(plan)};
    }
};

} // namespace wx360::xex
