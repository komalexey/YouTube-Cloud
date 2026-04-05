#include "youtube_cloud/decoder.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <sys/wait.h>

namespace youtube_cloud {

namespace {

bool pipeExitedSuccessfully(int status) {
    return status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

std::uint8_t quantizeChannel(std::uint8_t value) {
    static constexpr int kStep = 85;
    const int rounded = (static_cast<int>(value) + (kStep / 2)) / kStep;
    return static_cast<std::uint8_t>(std::clamp(rounded, 0, 3));
}

double parseRate(const std::string& value) {
    const auto slash = value.find('/');
    if (slash == std::string::npos) {
        return value.empty() ? 0.0 : std::stod(value);
    }

    const auto numerator = std::stod(value.substr(0, slash));
    const auto denominator = std::stod(value.substr(slash + 1));
    if (denominator == 0.0) {
        return 0.0;
    }
    return numerator / denominator;
}

std::size_t findSequence(const std::vector<std::uint8_t>& haystack,
                         const std::vector<std::uint8_t>& needle) {
    if (needle.empty() || haystack.size() < needle.size()) {
        return std::string::npos;
    }

    const auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end());
    if (it == haystack.end()) {
        return std::string::npos;
    }
    return static_cast<std::size_t>(std::distance(haystack.begin(), it));
}

}  // namespace

YouTubeDecoder::YouTubeDecoder(std::string key, CodecSettings settings, ProgressSink* sink)
    : key_(std::move(key)), settings_(settings), sink_(sink != nullptr ? sink : &null_sink_) {
    precomputeCoordinates();
}

bool YouTubeDecoder::decode(const std::filesystem::path& video_file,
                            const std::filesystem::path& output_dir) {
    if (settings_.palette_colors != 16 && settings_.palette_colors != 64) {
        throw std::runtime_error("Unsupported palette size. Use 16 or 64.");
    }

    if (!std::filesystem::exists(video_file)) {
        log("ERROR: video file not found: " + video_file.string());
        return false;
    }

    log("============================================================");
    log("YouTube Decoder");
    log("============================================================");
    log("Grid: " + std::to_string(settings_.blocksX()) + " x " + std::to_string(settings_.blocksY()));
    log("Palette: " + std::to_string(settings_.palette_colors) + " colors (" +
        std::to_string(bitsPerSymbol(static_cast<std::size_t>(settings_.palette_colors))) +
        " bits per block)");
    log("Key: " + std::string(key_.empty() ? "NO" : "YES"));

    const auto info = probeVideo(video_file);
    log("Video frames: " + std::to_string(info.frame_count));
    log("Video FPS: " + std::to_string(info.fps));
    log("Video resolution: " + std::to_string(info.width) + "x" + std::to_string(info.height));

    const std::string command =
        "ffmpeg -loglevel error -i " + shellEscape(video_file) +
        " -vf scale=" + std::to_string(settings_.width) + ":" + std::to_string(settings_.height) +
        ":flags=neighbor -f rawvideo -pix_fmt bgr24 -an -sn -";

    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        throw std::runtime_error("Unable to start ffmpeg for reading");
    }

    const auto frame_bytes =
        static_cast<std::size_t>(settings_.width) * static_cast<std::size_t>(settings_.height) * 3U;
    std::vector<std::uint8_t> frame(frame_bytes, 0);
    std::vector<std::uint32_t> sampled_colors;
    sampled_colors.reserve(info.frame_count > 0 ? info.frame_count * settings_.blocksPerRegion() : 0);

    std::size_t frames_processed = 0;
    const auto started = std::chrono::steady_clock::now();

    while (true) {
        std::size_t received = 0;
        while (received < frame.size()) {
            const auto chunk = std::fread(frame.data() + received, 1, frame.size() - received, pipe);
            if (chunk == 0) {
                break;
            }
            received += chunk;
        }

        if (received == 0) {
            break;
        }
        if (received < frame.size()) {
            break;
        }

        auto frame_samples = sampleFrame(frame);
        sampled_colors.insert(sampled_colors.end(), frame_samples.begin(), frame_samples.end());

        ++frames_processed;
        if (frames_processed % 100 == 0) {
            const auto elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            const auto speed = elapsed > 0.0 ? static_cast<double>(frames_processed) / elapsed : 0.0;
            log("Progress: " + std::to_string(frames_processed) + "/" + std::to_string(info.frame_count) +
                " | speed: " + std::to_string(speed) + " frames/sec");
        }
        reportProgress("decode", frames_processed, info.frame_count);
    }

    const int status = pclose(pipe);
    if (!pipeExitedSuccessfully(status)) {
        log("ERROR: ffmpeg failed while decoding the video");
        return false;
    }

    const auto elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    log("Sampled blocks: " + std::to_string(sampled_colors.size()) + " in " + std::to_string(elapsed) + " sec");
    log("Frames processed: " + std::to_string(frames_processed));

    return recoverFileFromSamples(sampled_colors,
                                  output_dir,
                                  bitsPerSymbol(static_cast<std::size_t>(settings_.palette_colors)),
                                  settings_.palette_colors == 16,
                                  true);
}

void YouTubeDecoder::log(const std::string& message) const {
    sink_->log(message);
}

void YouTubeDecoder::reportProgress(const std::string& stage,
                                    std::size_t current,
                                    std::size_t total) const {
    sink_->progress(stage, current, total);
}

void YouTubeDecoder::precomputeCoordinates() {
    block_coords_.clear();
    block_coords_.reserve(settings_.blocksPerRegion());

    for (std::size_t index = 0; index < settings_.blocksPerRegion(); ++index) {
        const int y = static_cast<int>(index / static_cast<std::size_t>(settings_.blocksX()));
        const int x = static_cast<int>(index % static_cast<std::size_t>(settings_.blocksX()));
        if (y < settings_.blocksY()) {
            const int cx =
                settings_.marker_size + x * (settings_.block_width + settings_.spacing) + settings_.block_width / 2;
            const int cy = settings_.marker_size + y * (settings_.block_height + settings_.spacing) +
                           settings_.block_height / 2;
            block_coords_.emplace_back(cx, cy);
        }
    }
}

std::vector<std::uint32_t> YouTubeDecoder::sampleFrame(const std::vector<std::uint8_t>& frame) const {
    std::vector<std::uint32_t> samples;
    samples.reserve(block_coords_.size());

    for (const auto& [cx, cy] : block_coords_) {
        const auto offset =
            (static_cast<std::size_t>(cy) * static_cast<std::size_t>(settings_.width) +
             static_cast<std::size_t>(cx)) *
            3U;
        const auto packed = static_cast<std::uint32_t>(frame[offset]) |
                            (static_cast<std::uint32_t>(frame[offset + 1]) << 8U) |
                            (static_cast<std::uint32_t>(frame[offset + 2]) << 16U);
        samples.push_back(packed);
    }

    return samples;
}

std::uint8_t YouTubeDecoder::packedColorToLegacySymbol(std::uint32_t packed_color) {
    const auto cached = color_cache_.find(packed_color);
    if (cached != color_cache_.end()) {
        ++cache_hits_;
        return cached->second;
    }

    ++cache_misses_;

    const auto blue = static_cast<std::uint8_t>(packed_color & 0xFFU);
    const auto green = static_cast<std::uint8_t>((packed_color >> 8U) & 0xFFU);
    const auto red = static_cast<std::uint8_t>((packed_color >> 16U) & 0xFFU);

    if (blue > 200 && green < 50 && red < 50) {
        color_cache_.emplace(packed_color, 0);
        return 0;
    }

    const auto& colors = legacyPalette();
    int best_distance = std::numeric_limits<int>::max();
    std::uint8_t best_index = 0;

    for (std::size_t index = 0; index < colors.size(); ++index) {
        const auto db = static_cast<int>(blue) - static_cast<int>(colors[index].b);
        const auto dg = static_cast<int>(green) - static_cast<int>(colors[index].g);
        const auto dr = static_cast<int>(red) - static_cast<int>(colors[index].r);
        const auto distance = db * db + dg * dg + dr * dr;
        if (distance < best_distance) {
            best_distance = distance;
            best_index = static_cast<std::uint8_t>(index);
        }
    }

    color_cache_.emplace(packed_color, best_index);
    return best_index;
}

std::uint8_t YouTubeDecoder::packedColorToExtendedSymbol(std::uint32_t packed_color) {
    const auto cached = color_cache_.find(packed_color);
    if (cached != color_cache_.end()) {
        ++cache_hits_;
        return cached->second;
    }

    ++cache_misses_;

    const auto blue = static_cast<std::uint8_t>(packed_color & 0xFFU);
    const auto green = static_cast<std::uint8_t>((packed_color >> 8U) & 0xFFU);
    const auto red = static_cast<std::uint8_t>((packed_color >> 16U) & 0xFFU);

    const auto b_index = quantizeChannel(blue);
    const auto g_index = quantizeChannel(green);
    const auto r_index = quantizeChannel(red);
    const auto symbol = static_cast<std::uint8_t>((b_index << 4U) | (g_index << 2U) | r_index);

    color_cache_.emplace(packed_color, symbol);
    return symbol;
}

bool YouTubeDecoder::recoverFileFromSamples(const std::vector<std::uint32_t>& sampled_colors,
                                            const std::filesystem::path& output_dir,
                                            int bits_per_symbol,
                                            bool legacy_mode,
                                            bool write_raw_on_failure) {
    cache_hits_ = 0;
    cache_misses_ = 0;
    color_cache_.clear();

    std::vector<std::uint8_t> symbols;
    symbols.reserve(sampled_colors.size());

    for (const auto packed_color : sampled_colors) {
        symbols.push_back(legacy_mode ? packedColorToLegacySymbol(packed_color)
                                      : packedColorToExtendedSymbol(packed_color));
    }

    log("Decoding mode: " + std::string(legacy_mode ? "legacy 16-color" : "extended 64-color"));
    log("Decoded blocks: " + std::to_string(symbols.size()));
    log("Cache hits: " + std::to_string(cache_hits_) + ", misses: " + std::to_string(cache_misses_));

    auto bytes_data = symbolsToBytes(symbols, bits_per_symbol);
    log("Decoded bytes: " + std::to_string(bytes_data.size()));

    const auto eof_pos = findSequence(bytes_data, eofMarkerBytes());
    if (eof_pos != std::string::npos && eof_pos > 0) {
        bytes_data.resize(eof_pos);
        log("EOF marker found at: " + std::to_string(eof_pos));
        log("Bytes after trim: " + std::to_string(bytes_data.size()));
    } else {
        log("WARNING: EOF marker not found");
    }

    const auto prefix_size = std::min<std::size_t>(1000, bytes_data.size());
    const std::string prefix(bytes_data.begin(), bytes_data.begin() + prefix_size);
    const std::regex pattern(R"(FILE:([^:]+):SIZE:(\d+)\|)");
    std::smatch match;

    if (std::regex_search(prefix, match, pattern)) {
        const auto filename = match[1].str();
        const auto filesize = static_cast<std::size_t>(std::stoull(match[2].str()));
        const auto header = match[0].str();
        const std::vector<std::uint8_t> header_bytes(header.begin(), header.end());
        const auto header_pos = findSequence(bytes_data, header_bytes);

        if (header_pos != std::string::npos) {
            const auto data_start = header_pos + header_bytes.size();
            const auto available = bytes_data.size() > data_start ? bytes_data.size() - data_start : 0;
            const auto payload_size = std::min(filesize, available);

            std::vector<std::uint8_t> encrypted_data(bytes_data.begin() + static_cast<std::ptrdiff_t>(data_start),
                                                     bytes_data.begin() + static_cast<std::ptrdiff_t>(data_start + payload_size));
            auto file_data = key_.empty() ? encrypted_data : xorWithKey(encrypted_data, key_);

            const auto output_path = uniqueOutputPath(output_dir, filename);
            writeBinaryFile(output_path, file_data);

            log("Recovered file: " + output_path.string());
            log("Recovered bytes: " + std::to_string(file_data.size()));
            if (file_data.size() == filesize) {
                log("Size check: OK");
            } else {
                log("Size check: mismatch, expected " + std::to_string(filesize));
            }
            return true;
        }
    }

    if (write_raw_on_failure) {
        const auto output_path = uniqueOutputPath(output_dir, "decoded_data.bin");
        writeBinaryFile(output_path, bytes_data);
        log("Header was not found. Raw data stored at: " + output_path.string());
    }

    return false;
}

VideoStreamInfo YouTubeDecoder::probeVideo(const std::filesystem::path& video_file) const {
    VideoStreamInfo info;
    const std::string command =
        "ffprobe -v error -select_streams v:0 -show_entries stream=width,height,avg_frame_rate,nb_frames "
        "-of default=noprint_wrappers=1 " +
        shellEscape(video_file);

    const auto output = runCommandCapture(command);
    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        const auto key = line.substr(0, separator);
        const auto value = line.substr(separator + 1);

        if (key == "width") {
            info.width = std::stoi(value);
        } else if (key == "height") {
            info.height = std::stoi(value);
        } else if (key == "avg_frame_rate") {
            info.fps = parseRate(value);
        } else if (key == "nb_frames" && !value.empty() && value != "N/A") {
            info.frame_count = static_cast<std::size_t>(std::stoull(value));
        }
    }

    return info;
}

}  // namespace youtube_cloud
