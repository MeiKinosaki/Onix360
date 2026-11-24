#include "wx360/gpu/xenos_command_processor.hpp"
#include <cstdlib>
#include <iostream>
#include <array>
#include <bit>

int main() {
    using namespace wx360::gpu;
    int failures = 0;
    const auto check = [&](bool ok, const char* label) {
        if (!ok) { std::cerr << "FAIL: " << label << '\\n'; ++failures; }
    };

    const std::array<std::uint32_t, 8> stream{
        (0U << 30U) | (1U << 16U) | 0x123U, 0xAABBCCDDU, 0x01020304U,
        (2U << 30U),
        (3U << 30U) | (1U << 16U) | (0x4AU << 8U) | 1U,
        0x11111111U, 0x22222222U,
        (1U << 30U)
    };
    std::size_t cursor = 0;
    Pm4PacketView packet{};
    XenosCommandProcessor processor;
    check(XenosCommandProcessor::decode_one(stream, cursor, packet) == Pm4DecodeResult::Ok &&
          packet.type == Pm4PacketType::Type0 && packet.register_index == 0x123U &&
          packet.payload.size() == 2 && packet.payload[0] == 0xAABBCCDDU,
          "decodes Type-0 register write packet");
    check(processor.execute_type0(packet) &&
          processor.register_value(0x123U) == 0xAABBCCDDU &&
          processor.register_value(0x124U) == 0x01020304U,
          "executes Type-0 consecutive register writes");
    Pm4PacketView overflow{};
    overflow.type = Pm4PacketType::Type0;
    overflow.register_index = 0x7FFFU;
    overflow.payload = std::span<const std::uint32_t>(stream.data(), 2);
    check(!processor.execute_type0(overflow) &&
          processor.register_value(0x7FFFU) == 0U,
          "rejects Type-0 register range overflow without partial writes");
    check(XenosCommandProcessor::decode_one(stream, cursor, packet) == Pm4DecodeResult::Ok &&
          packet.type == Pm4PacketType::Type2 && packet.dword_count == 1,
          "decodes Type-2 padding packet");
    check(XenosCommandProcessor::decode_one(stream, cursor, packet) == Pm4DecodeResult::Ok &&
          packet.type == Pm4PacketType::Type3 && packet.opcode == 0x4AU &&
          packet.predicate && packet.payload.size() == 2,
          "decodes Type-3 packet and predicate bit");
    check(XenosCommandProcessor::decode_one(stream, cursor, packet) ==
          Pm4DecodeResult::UnsupportedPacketType,
          "rejects unsupported Type-1 packet without consuming it");
    check(cursor == 7, "cursor remains at unsupported packet");

    const std::array<std::uint32_t, 2> truncated{
        (3U << 30U) | (3U << 16U) | (0x10U << 8U), 0x12345678U
    };
    cursor = 0;
    check(XenosCommandProcessor::decode_one(truncated, cursor, packet) ==
          Pm4DecodeResult::TruncatedPacket && cursor == 0,
          "rejects truncated PM4 packet without advancing cursor");
    const std::array<std::uint32_t, 1> empty_payload{
        (0U << 30U) | (0x20U << 16U)
    };
    cursor = 0;
    check(XenosCommandProcessor::decode_one(empty_payload, cursor, packet) ==
          Pm4DecodeResult::TruncatedPacket,
          "rejects truncated Type-0 payload");
    cursor = 1;
    check(XenosCommandProcessor::decode_one(empty_payload, cursor, packet) ==
          Pm4DecodeResult::EndOfStream,
          "reports end of command stream");

    // WX360-only software draw packet: 3 XY pairs (float bit patterns) + RGBA.
    XenosCommandProcessor draw;
    check(draw.configure_framebuffer(32, 32), "configures command processor framebuffer");
    const std::array<std::uint32_t, 7> tri{
        std::bit_cast<std::uint32_t>(4.0F), std::bit_cast<std::uint32_t>(4.0F),
        std::bit_cast<std::uint32_t>(24.0F), std::bit_cast<std::uint32_t>(4.0F),
        std::bit_cast<std::uint32_t>(4.0F), std::bit_cast<std::uint32_t>(24.0F),
        0xFF55AA33U};
    Pm4PacketView draw_packet{Pm4PacketType::Type3,
        XenosCommandProcessor::wx360_draw_triangle_opcode, 0, false, tri, 8};
    check(draw.execute_packet(draw_packet), "executes WX360 software triangle packet");
    check(draw.framebuffer().pixels()[10U * 32U + 10U] == 0xFF55AA33U,
          "PM4 software triangle reaches framebuffer");
    draw_packet.payload = std::span<const std::uint32_t>(tri.data(), 6);
    check(!draw.execute_packet(draw_packet), "rejects malformed software draw packet");
    XenosCommandProcessor stream_gpu;
    check(stream_gpu.configure_framebuffer(8, 8), "configures stream framebuffer");
    const std::array<std::uint32_t, 4> clear_present{
        (3U << 30U) | (0xF1U << 8U), 0xFF123456U,
        (3U << 30U) | (0xF2U << 8U), 0U
    };
    check(stream_gpu.execute_stream(clear_present), "executes clear and present stream");
    check(stream_gpu.framebuffer().pixels()[0] == 0xFF123456U &&
          stream_gpu.framebuffer().front_buffer()[0] == 0xFF123456U &&
          stream_gpu.framebuffer().present_count() == 1U,
          "clear and present update front buffer");
    const std::array<std::uint32_t, 2> unknown_stream{
        (3U << 30U) | (0xE1U << 8U), 0U
    };
    check(!stream_gpu.execute_stream(unknown_stream), "rejects unknown PM4 command");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
