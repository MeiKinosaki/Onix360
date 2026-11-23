#include "wx360/storage/game_library.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
void be32(std::vector<std::uint8_t>& b, std::size_t p, std::uint32_t v) {
    b[p] = static_cast<std::uint8_t>(v >> 24);
    b[p + 1] = static_cast<std::uint8_t>(v >> 16);
    b[p + 2] = static_cast<std::uint8_t>(v >> 8);
    b[p + 3] = static_cast<std::uint8_t>(v);
}
std::vector<std::uint8_t> valid_xex() {
    std::vector<std::uint8_t> b(0x500, 0);
    b[0]='X'; b[1]='E'; b[2]='X'; b[3]='2';
    be32(b, 8, 0x400); be32(b, 16, 0x100); be32(b, 20, 2);
    be32(b, 24, 0x00010100); be32(b, 28, 0x82000000);
    be32(b, 32, 0x000003FF); be32(b, 36, 0x300);
    be32(b, 0x100, 0x184); be32(b, 0x104, 0x100);
    be32(b, 0x300, 8);
    b[0x304]=0; b[0x305]=0; b[0x306]=0; b[0x307]=0;
    return b;
}
void write(const std::filesystem::path& p, const std::vector<std::uint8_t>& b) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}
}

int main() {
    namespace fs = std::filesystem;
    using namespace wx360;
    const auto root = fs::temp_directory_path() / "wx360_library_xex_open_tests";
    std::error_code ec; fs::remove_all(root, ec); fs::create_directories(root, ec);
    const auto valid = root / "valid.xex";
    const auto truncated = root / "truncated.xex";
    const auto bad_magic = root / "bad_magic.xex";
    const auto inconsistent = root / "inconsistent.xex";
    write(valid, valid_xex());
    write(truncated, std::vector<std::uint8_t>{'X','E','X','2',0,0,0,0});
    auto bad = valid_xex(); bad[0]='N'; write(bad_magic, bad);
    auto mismatch = valid_xex(); be32(mismatch, 0x104, 0x80); write(inconsistent, mismatch);

    const auto library = storage::GameLibrary::scan(root);
    check(library.size() == 4, "library discovers all XEX test fixtures");
    xex::FullXexImage output{};
    const auto find = [&](const fs::path& p) -> const storage::GameFile* {
        for (const auto& item : library) if (item.path == p) return &item;
        return nullptr;
    };
    const auto* good = find(valid);
    check(good != nullptr, "valid XEX appears in library");
    if (good) {
        const auto status = storage::GameLibrary::open_xex(*good, output);
        check(status == xex::LoadError::None, "valid library entry opens complete XEX metadata");
        check(output.header.entry_point == 0x82000000, "valid XEX entry point is read");
        check(output.bytes.size() == 0x500, "complete XEX bytes are read");
    }
    const auto expect = [&](const fs::path& p, xex::LoadError wanted, const char* msg) {
        const auto* item = find(p);
        check(item != nullptr, "invalid fixture remains discoverable for error reporting");
        if (item) check(storage::GameLibrary::open_xex(*item, output) == wanted, msg);
    };
    expect(truncated, xex::LoadError::InvalidFileSize, "truncated file is rejected");
    expect(bad_magic, xex::LoadError::InvalidHeader, "invalid XEX2 magic is rejected");
    expect(inconsistent, xex::LoadError::SecurityInfoInvalid,
           "SecurityInfo/FileFormatInfo image-size mismatch is rejected");
    storage::GameFile forged{valid, "ISO"};
    check(storage::GameLibrary::open_xex(forged, output) == xex::LoadError::InvalidHeader,
          "non-XEX library entry cannot be passed as XEX");
    fs::remove_all(root, ec);
    if (failures == 0) std::cout << "WX360 library XEX open tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
