#pragma once

#include "wx360/xex/xex_header.hpp"
#include "wx360/xex/xex_security_info.hpp"
#include "wx360/crypto/aes128.hpp"
#include "wx360/xex/xex_pe_mapper.hpp"
#include "wx360/kernel/xex_import_resolver.hpp"
#include "wx360/compression/lzx_decoder.hpp"
#include <bit>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
};

struct LaunchPlan {
    Header header{};
    std::filesystem::path source;
    std::uint64_t file_size{};
    std::uint32_t entry_point{}; // XEX virtual entry point
    std::uint64_t mapped_entry_point{}; // guest physical address after PE/MMU mapping
    bool decryption_complete{};
    bool decompression_complete{};
    bool pe_mapped{};
    bool kernel_imports_resolved{};
};

struct FullXexImage {
    std::vector<std::uint8_t> bytes;
    Header header{};
    SecurityInfo security{};
    FileFormatInfo format{};
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
        LaunchPlan& plan,
        std::span<const kernel::XexImport> imports,
        const kernel::XexImportResolver& resolver,
        memory::PhysicalMemory& guest_memory) {
        const auto status = resolver.resolve(imports, guest_memory);
        plan.kernel_imports_resolved = status == kernel::ImportResolveError::None;
        return status;
    }

    [[nodiscard]] static LoadError map_transformed_pe(
        LaunchPlan& plan,
        std::span<const std::uint8_t> decrypted_decompressed_pe,
        memory::PhysicalMemory& guest_memory,
        std::uint64_t physical_base,
        std::uint32_t guest_image_base,
        bool decryption_complete,
        bool decompression_complete) {
        if (!decryption_complete || !decompression_complete)
            return LoadError::TransformIncomplete;
        const auto mapped = XexPeMapper::map(
            decrypted_decompressed_pe, guest_memory, physical_base,
            guest_image_base, plan.entry_point);
        if (!mapped) return LoadError::PeMappingFailed;
        plan.decryption_complete = true;
        plan.decompression_complete = true;
        plan.pe_mapped = true;
        plan.mapped_entry_point = mapped.mapped_entry;
        return LoadError::None;
    }

    // Safe end-to-end path for XEX images explicitly marked unencrypted and
    // uncompressed. The loader derives completion from parsed metadata rather
    // than accepting caller-supplied success flags. This does not authenticate
    // the RSA signature or claim retail/devkit trust.
    [[nodiscard]] static LoadError map_uncompressed_pe(
        const FullXexImage& image,
        LaunchPlan& plan,
        memory::PhysicalMemory& guest_memory,
        std::uint64_t physical_base,
        std::uint32_t guest_image_base) {
        if (image.format.encryption != XexEncryption::None ||
            image.format.compression != XexCompression::None)
            return LoadError::TransformIncomplete;
        const auto offset = static_cast<std::size_t>(image.header.pe_data_offset);
        const auto length = static_cast<std::size_t>(image.format.image_size);
        if (offset > image.bytes.size() || length > image.bytes.size() - offset ||
            length != image.security.image_size)
            return LoadError::ImageNotMapped;
        const auto pe = std::span<const std::uint8_t>(image.bytes).subspan(offset, length);
        const auto mapped = XexPeMapper::map(
            pe, guest_memory, physical_base, guest_image_base, plan.entry_point);
        if (!mapped) return LoadError::PeMappingFailed;
        plan.decryption_complete = true;
        plan.decompression_complete = true;
        plan.pe_mapped = true;
        plan.mapped_entry_point = mapped.mapped_entry;
        return LoadError::None;
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
        output = std::move(parsed);
        return LoadError::None;
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
