#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace youtube_cloud {

struct Color {
    std::uint8_t b = 0;
    std::uint8_t g = 0;
    std::uint8_t r = 0;
};

struct CodecSettings {
    int width = 1920;
    int height = 1080;
    int fps = 6;
    int block_height = 16;
    int block_width = 24;
    int spacing = 4;
    int marker_size = 80;
    int protective_frames = 5;

    [[nodiscard]] int blocksX() const;
    [[nodiscard]] int blocksY() const;
    [[nodiscard]] std::size_t blocksPerRegion() const;
};

struct VideoStreamInfo {
    int width = 0;
    int height = 0;
    double fps = 0.0;
    std::size_t frame_count = 0;
};

const std::array<Color, 16>& legacyPalette();
int bitsPerSymbol(std::size_t color_count);
Color colorFrom64Symbol(std::uint8_t symbol);

std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path& path);
void writeBinaryFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& data);
std::vector<std::uint8_t> xorWithKey(const std::vector<std::uint8_t>& data, const std::string& key);
std::vector<std::uint8_t> bytesToSymbols(const std::vector<std::uint8_t>& data, int bits_per_symbol);
std::vector<std::uint8_t> symbolsToBytes(const std::vector<std::uint8_t>& symbols, int bits_per_symbol);
std::vector<std::uint8_t> eofMarkerBytes();
std::string readKeyFromFile(const std::filesystem::path& path);
std::filesystem::path uniqueOutputPath(const std::filesystem::path& directory, const std::string& filename);
std::string shellEscape(const std::filesystem::path& path);
std::string runCommandCapture(const std::string& command);

}  // namespace youtube_cloud
