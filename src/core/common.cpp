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

const std::array<Color, 16>& palette() {
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

std::vector<std::uint8_t> bytesToNibbles(const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> nibbles;
    nibbles.reserve(data.size() * 2);

    for (const auto byte : data) {
        nibbles.push_back(static_cast<std::uint8_t>((byte >> 4) & 0x0F));
        nibbles.push_back(static_cast<std::uint8_t>(byte & 0x0F));
    }

    return nibbles;
}

std::vector<std::uint8_t> nibblesToBytes(const std::vector<std::uint8_t>& nibbles) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(nibbles.size() / 2);

    for (std::size_t index = 0; index + 1 < nibbles.size(); index += 2) {
        const auto byte = static_cast<std::uint8_t>((nibbles[index] << 4) | (nibbles[index + 1] & 0x0F));
        bytes.push_back(byte);
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
