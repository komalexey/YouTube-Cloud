#include "youtube_cloud/common.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace youtube_cloud {

namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

}  // namespace

int CodecSettings::blocksX() const {
    return (width - 2 * marker_size) / (block_width + spacing);
}

int CodecSettings::blocksY() const {
    return (height - 2 * marker_size) / (block_height + spacing);
}

std::size_t CodecSettings::blocksPerRegion() const {
    return static_cast<std::size_t>(blocksX()) * static_cast<std::size_t>(blocksY());
}

const std::array<Color, 16>& legacyPalette() {
    static const std::array<Color, 16> colors = {{
        {255, 0, 0},
        {0, 255, 0},
        {0, 0, 255},
        {255, 255, 0},
        {255, 0, 255},
        {0, 255, 255},
        {255, 128, 0},
        {128, 0, 255},
        {0, 128, 128},
        {128, 128, 0},
        {128, 0, 128},
        {0, 128, 0},
        {128, 0, 0},
        {0, 0, 128},
        {192, 192, 192},
        {255, 255, 255},
    }};
    return colors;
}

int bitsPerSymbol(std::size_t color_count) {
    int bits = 0;
    std::size_t current = 1;
    while (current < color_count) {
        current <<= 1U;
        ++bits;
    }
    return bits;
}

Color colorFrom64Symbol(std::uint8_t symbol) {
    static constexpr std::array<std::uint8_t, 4> kLevels = {0, 85, 170, 255};
    const auto b = kLevels[(symbol >> 4U) & 0x03U];
    const auto g = kLevels[(symbol >> 2U) & 0x03U];
    const auto r = kLevels[symbol & 0x03U];
    return {b, g, r};
}

std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open file for reading: " + path.string());
    }

    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                     std::istreambuf_iterator<char>());
}

void writeBinaryFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("Unable to open file for writing: " + path.string());
    }

    output.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data.size()));
    if (!output) {
        throw std::runtime_error("Unable to write file: " + path.string());
    }
}

std::vector<std::uint8_t> xorWithKey(const std::vector<std::uint8_t>& data, const std::string& key) {
    if (key.empty()) {
        return data;
    }

    std::vector<std::uint8_t> result(data.size(), 0);
    for (std::size_t index = 0; index < data.size(); ++index) {
        const auto key_byte = static_cast<std::uint8_t>(key[index % key.size()]);
        result[index] = static_cast<std::uint8_t>(data[index] ^ key_byte);
    }
    return result;
}

std::vector<std::uint8_t> bytesToSymbols(const std::vector<std::uint8_t>& data, int bits_per_symbol) {
    std::vector<std::uint8_t> symbols;
    if (bits_per_symbol <= 0 || bits_per_symbol > 8) {
        return symbols;
    }

    std::uint32_t bit_buffer = 0;
    int buffered_bits = 0;
    const auto mask = static_cast<std::uint32_t>((1U << bits_per_symbol) - 1U);

    for (const auto byte : data) {
        bit_buffer = (bit_buffer << 8U) | static_cast<std::uint32_t>(byte);
        buffered_bits += 8;

        while (buffered_bits >= bits_per_symbol) {
            buffered_bits -= bits_per_symbol;
            symbols.push_back(static_cast<std::uint8_t>((bit_buffer >> buffered_bits) & mask));
        }
    }

    if (buffered_bits > 0) {
        symbols.push_back(static_cast<std::uint8_t>((bit_buffer << (bits_per_symbol - buffered_bits)) & mask));
    }

    return symbols;
}

std::vector<std::uint8_t> symbolsToBytes(const std::vector<std::uint8_t>& symbols, int bits_per_symbol) {
    std::vector<std::uint8_t> bytes;
    if (bits_per_symbol <= 0 || bits_per_symbol > 8) {
        return bytes;
    }

    std::uint32_t bit_buffer = 0;
    int buffered_bits = 0;
    const auto mask = static_cast<std::uint32_t>((1U << bits_per_symbol) - 1U);

    for (const auto symbol : symbols) {
        bit_buffer = (bit_buffer << bits_per_symbol) | (static_cast<std::uint32_t>(symbol) & mask);
        buffered_bits += bits_per_symbol;

        while (buffered_bits >= 8) {
            buffered_bits -= 8;
            bytes.push_back(static_cast<std::uint8_t>((bit_buffer >> buffered_bits) & 0xFFU));
        }
    }

    return bytes;
}

std::vector<std::uint8_t> eofMarkerBytes() {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(64 * 3);
    for (int index = 0; index < 64; ++index) {
        bytes.push_back(0xE2);
        bytes.push_back(0x96);
        bytes.push_back(0x88);
    }
    return bytes;
}

std::string readKeyFromFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        return {};
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return trim(buffer.str());
}

std::filesystem::path uniqueOutputPath(const std::filesystem::path& directory, const std::string& filename) {
    std::filesystem::create_directories(directory);

    std::filesystem::path output = directory / filename;
    if (!std::filesystem::exists(output)) {
        return output;
    }

    const auto stem = output.stem().string();
    const auto extension = output.extension().string();

    int counter = 1;
    while (true) {
        output = directory / (stem + "_" + std::to_string(counter) + extension);
        if (!std::filesystem::exists(output)) {
            return output;
        }
        ++counter;
    }
}

std::string shellEscape(const std::filesystem::path& path) {
    const auto raw = path.string();
    std::string escaped;
    escaped.reserve(raw.size() + 2);
    escaped.push_back('\'');

    for (const auto ch : raw) {
        if (ch == '\'') {
            escaped += "'\\''";
        } else {
            escaped.push_back(ch);
        }
    }

    escaped.push_back('\'');
    return escaped;
}

std::string runCommandCapture(const std::string& command) {
    std::array<char, 4096> buffer{};
    std::string output;

    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        throw std::runtime_error("Unable to start command: " + command);
    }

    while (true) {
        const auto read = std::fread(buffer.data(), sizeof(char), buffer.size(), pipe);
        if (read > 0) {
            output.append(buffer.data(), read);
        }
        if (read < buffer.size()) {
            break;
        }
    }

    const int status = pclose(pipe);
    if (status != 0) {
        throw std::runtime_error("Command failed: " + command);
    }

    return output;
}

}  // namespace youtube_cloud
