#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <array>

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
    std::array<std::uint32_t, 0x8000> registers_{};
};

} // namespace wx360::gpu
