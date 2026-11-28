#include "onix360/xex/xex_normal_blocks.hpp"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static void be32(std::vector<std::uint8_t>& b, std::size_t p, std::uint32_t v) {
    b[p] = static_cast<std::uint8_t>(v >> 24);
    b[p + 1] = static_cast<std::uint8_t>(v >> 16);
    b[p + 2] = static_cast<std::uint8_t>(v >> 8);
    b[p + 3] = static_cast<std::uint8_t>(v);
}
static void descriptor(std::vector<std::uint8_t>& b, std::size_t p,
                       const std::vector<std::uint8_t>& next) {
    be32(b, p, static_cast<std::uint32_t>(next.size()));
    const auto digest = onix360::crypto::sha1(next);
    std::copy(digest.begin(), digest.end(), b.begin() + static_cast<std::ptrdiff_t>(p + 4));
}
static std::vector<std::uint8_t> make_block(const std::string& payload) {
    std::vector<std::uint8_t> block(24 + 2 + payload.size() + 2, 0);
    block[24] = static_cast<std::uint8_t>(payload.size() >> 8);
    block[25] = static_cast<std::uint8_t>(payload.size());
    std::copy(payload.begin(), payload.end(), block.begin() + 26);
    return block;
}
int main() {
    using namespace onix360::xex;

    // Single authenticated block with a u16-sized chunk and terminator.
    auto single = make_block("hello");
    NormalBlockDescriptor first{};
    first.block_size = static_cast<std::uint32_t>(single.size());
    first.hash = onix360::crypto::sha1(single);
    std::vector<std::uint8_t> output;
    if (unpack_normal_chunks(single, first, 5, output) != NormalBlockError::Ok ||
        output.size() != 5 || std::string(output.begin(), output.end()) != "hello") {
        std::cerr << "XEX Normal single-block extraction failed\n";
        return EXIT_FAILURE;
    }

    // Two linked blocks. The first block embeds the descriptor (size + SHA-1)
    // of the second block; each SHA-1 authenticates the complete following
    // block payload, including its embedded next-block descriptor.
    auto second = make_block("world");
    std::vector<std::uint8_t> first_block(24 + 2 + 5 + 2, 0);
    descriptor(first_block, 0, second);
    first_block[24] = 0;
    first_block[25] = 5;
    std::copy_n("hello", 5, first_block.begin() + 26);
    NormalBlockDescriptor chain{};
    chain.block_size = static_cast<std::uint32_t>(first_block.size());
    chain.hash = onix360::crypto::sha1(first_block);
    std::vector<std::uint8_t> chained = first_block;
    chained.insert(chained.end(), second.begin(), second.end());
    if (unpack_normal_chunks(chained, chain, 10, output) != NormalBlockError::Ok ||
        std::string(output.begin(), output.end()) != "helloworld") {
        std::cerr << "XEX Normal chained-block extraction failed\n";
        return EXIT_FAILURE;
    }

    single[27] ^= 1;
    if (unpack_normal_chunks(single, first, 5, output) != NormalBlockError::HashMismatch) {
        std::cerr << "Corrupted Normal block was not rejected\n";
        return EXIT_FAILURE;
    }
    std::cout << "XEX Normal single/chained block framing and SHA-1 tests passed\n";
    return EXIT_SUCCESS;
}
