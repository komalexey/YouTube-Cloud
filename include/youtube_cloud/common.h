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

const std::array<Color, 16>& palette();

std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path& path);
void writeBinaryFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& data);
std::vector<std::uint8_t> xorWithKey(const std::vector<std::uint8_t>& data, const std::string& key);
std::vector<std::uint8_t> bytesToNibbles(const std::vector<std::uint8_t>& data);
std::vector<std::uint8_t> nibblesToBytes(const std::vector<std::uint8_t>& nibbles);
std::vector<std::uint8_t> eofMarkerBytes();
std::string readKeyFromFile(const std::filesystem::path& path);
std::filesystem::path uniqueOutputPath(const std::filesystem::path& directory, const std::string& filename);
std::string shellEscape(const std::filesystem::path& path);
std::string runCommandCapture(const std::string& command);

}  // namespace youtube_cloud
