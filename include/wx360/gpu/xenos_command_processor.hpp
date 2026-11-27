#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <array>
#include <vector>
#include <bit>
#include "wx360/gpu/xenos_framebuffer.hpp"

namespace wx360::gpu {

// Bounds-checked decoder for the outer Xbox 360 PM4 packet envelope.
// It deliberately does not claim that an opcode is implemented by the GPU.
enum class Pm4DecodeResult : std::uint8_t {
    Ok, EndOfStream, TruncatedPacket, UnsupportedPacketType
};

enum class Pm4PacketType : std::uint8_t { Type0 = 0, Type1 = 1, Type2 = 2, Type3 = 3 };

struct Pm4PacketView {
    Pm4PacketType type{Pm4PacketType::Type2};
    std::uint8_t opcode{};
    std::uint16_t register_index{};
    bool predicate{};
    std::span<const std::uint32_t> payload{};
    std::size_t dword_count{}; // Header plus payload.
};

class XenosCommandProcessor {
public:
    // WX360 development extension: 0xF0 carries three XY float pairs and RGBA.
    // It is NOT asserted to be a native Xbox 360 Xenos PM4 opcode.
    static constexpr std::uint8_t wx360_draw_triangle_opcode = 0xF0U;
    static constexpr std::uint8_t wx360_clear_opcode = 0xF1U;
    static constexpr std::uint8_t wx360_present_opcode = 0xF2U;
    static constexpr std::uint8_t wx360_texture_blit_opcode = 0xF3U;
    [[nodiscard]] bool configure_framebuffer(std::uint32_t width, std::uint32_t height) {
        return framebuffer_.resize(width, height);
    }
    [[nodiscard]] bool execute_packet(const Pm4PacketView& packet) noexcept {
        if (packet.type == Pm4PacketType::Type0) return execute_type0(packet);
        if (packet.type == Pm4PacketType::Type2) return packet.payload.empty();
        if (packet.type != Pm4PacketType::Type3) return false;
        if (packet.opcode == wx360_clear_opcode && packet.payload.size() == 1U) {
            framebuffer_.clear(packet.payload[0]);
            return true;
        }
        if (packet.opcode == wx360_present_opcode && packet.payload.size() == 1U && packet.payload[0] == 0U) return framebuffer_.present();
        if (packet.opcode == wx360_texture_blit_opcode && packet.payload.size() >= 7U) {
            const auto& p = packet.payload;
            const std::uint64_t pixel_count =
                static_cast<std::uint64_t>(p[0]) * p[1];
            if (pixel_count != p.size() - 6U || pixel_count > 1024U * 1024U)
                return false;
            return framebuffer_.blit_rgba8(p.subspan(6), p[0], p[1],
                p[2], p[3], p[4], p[5]);
        }
        if (packet.opcode != wx360_draw_triangle_opcode || packet.payload.size() != 7U) return false;
        const auto& p = packet.payload;
        return framebuffer_.draw_triangle(
            std::bit_cast<float>(p[0]), std::bit_cast<float>(p[1]),
            std::bit_cast<float>(p[2]), std::bit_cast<float>(p[3]),
            std::bit_cast<float>(p[4]), std::bit_cast<float>(p[5]), p[6]);
    }
    [[nodiscard]] const XenosFramebuffer& framebuffer() const noexcept { return framebuffer_; }

    // Executes a complete bounded stream; unsupported packets fail closed.
    [[nodiscard]] bool execute_stream(std::span<const std::uint32_t> stream) noexcept {
        std::size_t cursor = 0;
        while (cursor < stream.size()) {
            Pm4PacketView packet{};
            const auto decoded = decode_one(stream, cursor, packet);
            if (decoded != Pm4DecodeResult::Ok || !execute_packet(packet)) return false;
        }
        return true;
    }

    // Type-0 packets write consecutive MMIO registers. The register file is
    // intentionally a bounded software model; device-specific side effects
    // are added only when their Xenos register semantics are implemented.
    [[nodiscard]] bool execute_type0(const Pm4PacketView& packet) noexcept {
        if (packet.type != Pm4PacketType::Type0 || packet.payload.empty() ||
            packet.register_index >= registers_.size() ||
            packet.payload.size() > registers_.size() - packet.register_index)
            return false;
        for (std::size_t i = 0; i < packet.payload.size(); ++i)
            registers_[packet.register_index + i] = packet.payload[i];
        return true;
    }

    [[nodiscard]] std::uint32_t register_value(std::uint16_t index) const noexcept {
        return index < registers_.size() ? registers_[index] : 0U;
    }
    [[nodiscard]] static Pm4DecodeResult decode_one(
        std::span<const std::uint32_t> stream, std::size_t& cursor,
        Pm4PacketView& packet) noexcept {
        if (cursor >= stream.size()) return Pm4DecodeResult::EndOfStream;
        const std::uint32_t header = stream[cursor];
        const auto raw_type = static_cast<std::uint8_t>(header >> 30U);
        if (raw_type == 1U) return Pm4DecodeResult::UnsupportedPacketType;

        if (raw_type == 2U) {
            packet = {Pm4PacketType::Type2, 0, 0, false, {}, 1};
            ++cursor;
            return Pm4DecodeResult::Ok;
        }

        const std::size_t encoded_count =
            static_cast<std::size_t>((header >> 16U) & 0x3FFFU);
        const std::size_t payload_count =
            raw_type == 0U ? encoded_count + 1U : encoded_count + 1U;
        if (payload_count > stream.size() - cursor - 1U)
            return Pm4DecodeResult::TruncatedPacket;

        if (raw_type == 0U) {
            packet = {Pm4PacketType::Type0, 0,
                      static_cast<std::uint16_t>(header & 0x7FFFU), false,
                      stream.subspan(cursor + 1U, payload_count), payload_count + 1U};
        } else {
            packet = {Pm4PacketType::Type3,
                      static_cast<std::uint8_t>((header >> 8U) & 0xFFU), 0,
                      (header & 1U) != 0,
                      stream.subspan(cursor + 1U, payload_count), payload_count + 1U};
        }
        cursor += payload_count + 1U;
        return Pm4DecodeResult::Ok;
    }
private:
    // Keep the 128 KiB MMIO register file off the host stack. XenonCore embeds
    // this processor and many machine instances can coexist in frontend/tests.
    std::vector<std::uint32_t> registers_ = std::vector<std::uint32_t>(0x8000U, 0U);
    XenosFramebuffer framebuffer_{};
};

} // namespace wx360::gpu
