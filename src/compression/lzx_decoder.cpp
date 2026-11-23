#include "wx360/compression/lzx_decoder.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

extern "C" {
#include <system.h>
#include <lzx.h>
}

namespace wx360::compression {
namespace {
struct MemoryFile {
    mspack_file base{};
    std::span<const std::uint8_t> input{};
    std::vector<std::uint8_t>* output{};
    std::size_t position{};
    std::size_t output_limit{};
};

struct MemorySystem {
    mspack_system api{};
};

mspack_file* open_file(mspack_system*, const char*, int) { return nullptr; }
void close_file(mspack_file*) {}
int read_file(mspack_file* file, void* buffer, int bytes) {
    if (!file || !buffer || bytes < 0) return -1;
    auto* memory = reinterpret_cast<MemoryFile*>(file);
    if (memory->position > memory->input.size()) return -1;
    const auto count = std::min<std::size_t>(
        static_cast<std::size_t>(bytes), memory->input.size() - memory->position);
    if (count) std::memcpy(buffer, memory->input.data() + memory->position, count);
    memory->position += count;
    return static_cast<int>(count);
}
int write_file(mspack_file* file, void* buffer, int bytes) {
    if (!file || !buffer || bytes < 0) return -1;
    auto* memory = reinterpret_cast<MemoryFile*>(file);
    if (!memory->output || static_cast<std::size_t>(bytes) >
            memory->output_limit - std::min(memory->output->size(), memory->output_limit))
        return -1;
    const auto* first = static_cast<const std::uint8_t*>(buffer);
    try {
        memory->output->insert(memory->output->end(), first, first + bytes);
    } catch (...) {
        return -1;
    }
    return bytes;
}
int seek_file(mspack_file* file, off_t offset, int mode) {
    if (!file || offset < 0) return 1;
    auto* memory = reinterpret_cast<MemoryFile*>(file);
    const auto base = mode == MSPACK_SYS_SEEK_START ? 0 :
        mode == MSPACK_SYS_SEEK_CUR ? memory->position :
        mode == MSPACK_SYS_SEEK_END ? memory->input.size() :
        std::numeric_limits<std::size_t>::max();
    if (base == std::numeric_limits<std::size_t>::max() ||
        static_cast<std::uint64_t>(offset) > memory->input.size() ||
        base > memory->input.size() - static_cast<std::size_t>(offset)) return 1;
    memory->position = base + static_cast<std::size_t>(offset);
    return 0;
}
off_t tell_file(mspack_file* file) {
    return file ? static_cast<off_t>(reinterpret_cast<MemoryFile*>(file)->position) : -1;
}
void message_file(mspack_file*, const char*, ...) {}
void* allocate_memory(mspack_system*, std::size_t bytes) { return std::malloc(bytes); }
void free_memory(void* memory) { std::free(memory); }
void copy_memory(void* source, void* destination, std::size_t bytes) {
    std::memcpy(destination, source, bytes);
}
} // namespace

LzxError decode_lzx(std::span<const std::uint8_t> input,
                    unsigned window_bits,
                    unsigned reset_interval,
                    std::size_t output_size,
                    std::vector<std::uint8_t>& output) {
    if (window_bits < 15 || window_bits > 21) return LzxError::InvalidWindow;
    if (reset_interval > static_cast<unsigned>(std::numeric_limits<int>::max()) ||
        output_size == 0 || output_size > static_cast<std::size_t>(
            std::numeric_limits<off_t>::max())) return LzxError::InvalidOutputSize;

    MemorySystem system{};
    system.api.open = open_file;
    system.api.close = close_file;
    system.api.read = read_file;
    system.api.write = write_file;
    system.api.seek = seek_file;
    system.api.tell = tell_file;
    system.api.message = message_file;
    system.api.alloc = allocate_memory;
    system.api.free = free_memory;
    system.api.copy = copy_memory;
    system.api.null_ptr = nullptr;

    MemoryFile source{};
    source.input = input;
    MemoryFile destination{};
    std::vector<std::uint8_t> decoded;
    try {
        decoded.reserve(output_size);
    } catch (const std::bad_alloc&) {
        return LzxError::DecoderInitFailed;
    } catch (const std::length_error&) {
        return LzxError::InvalidOutputSize;
    }
    destination.output = &decoded;
    destination.output_limit = output_size;

    auto* stream = lzxd_init(&system.api, &source.base, &destination.base,
        static_cast<int>(window_bits), static_cast<int>(reset_interval),
        4096, static_cast<off_t>(output_size), 0);
    if (!stream) return LzxError::DecoderInitFailed;
    const int status = lzxd_decompress(stream, static_cast<off_t>(output_size));
    lzxd_free(stream);
    if (status != MSPACK_ERR_OK || decoded.size() != output_size)
        return LzxError::DecodeFailed;
    output = std::move(decoded);
    return LzxError::None;
}
} // namespace wx360::compression
