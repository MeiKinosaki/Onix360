#include "onix360/gpu/xenos_command_processor.hpp"
#include <cstdlib>
#include <iostream>
#include <array>
#include <bit>

int main() {
    using namespace onix360::gpu;
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

    // Onix360-only software draw packet: 3 XY pairs (float bit patterns) + RGBA.
    XenosCommandProcessor draw;
    check(draw.configure_framebuffer(32, 32), "configures command processor framebuffer");
    const std::array<std::uint32_t, 7> tri{
        std::bit_cast<std::uint32_t>(4.0F), std::bit_cast<std::uint32_t>(4.0F),
        std::bit_cast<std::uint32_t>(24.0F), std::bit_cast<std::uint32_t>(4.0F),
        std::bit_cast<std::uint32_t>(4.0F), std::bit_cast<std::uint32_t>(24.0F),
        0xFF55AA33U};
    Pm4PacketView draw_packet{Pm4PacketType::Type3,
        XenosCommandProcessor::onix360_draw_triangle_opcode, 0, false, tri, 8};
    check(draw.execute_packet(draw_packet), "executes Onix360 software triangle packet");
    check(draw.framebuffer().pixels()[10U * 32U + 10U] == 0xFF55AA33U,
          "PM4 software triangle reaches framebuffer");
    draw_packet.payload = std::span<const std::uint32_t>(tri.data(), 6);
    check(!draw.execute_packet(draw_packet), "rejects malformed software draw packet");
    XenosCommandProcessor texture_gpu;
    check(texture_gpu.configure_framebuffer(8, 8), "configures texture command framebuffer");
    const std::array<std::uint32_t, 10> texture_command{
        2U, 2U, 1U, 1U, 4U, 4U,
        0xFF000001U, 0xFF000002U, 0xFF000003U, 0xFF000004U};
    Pm4PacketView texture_packet{Pm4PacketType::Type3,
        XenosCommandProcessor::onix360_texture_blit_opcode, 0, false,
        texture_command, texture_command.size() + 1U};
    check(texture_gpu.execute_packet(texture_packet),
          "executes bounded software texture blit packet");
    check(texture_gpu.framebuffer().pixels()[1U * 8U + 1U] == 0xFF000001U &&
          texture_gpu.framebuffer().pixels()[4U * 8U + 4U] == 0xFF000004U,
          "texture packet writes scaled texels to framebuffer");
    const std::array<std::uint32_t, 7> invalid_texture{
        3U, 3U, 0U, 0U, 4U, 4U, 0xFFFFFFFFU};
    texture_packet.payload = invalid_texture;
    check(!texture_gpu.execute_packet(texture_packet),
          "rejects texture packet with inconsistent pixel payload");

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
