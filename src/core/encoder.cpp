#include "youtube_cloud/encoder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <sys/wait.h>

namespace youtube_cloud {

namespace {

constexpr Color kBlack{0, 0, 0};
constexpr Color kWhite{255, 255, 255};
constexpr Color kBlue{255, 0, 0};

bool pipeExitedSuccessfully(int status) {
    return status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

std::string humanMegabytes(std::uintmax_t bytes) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(2);
    stream << (static_cast<double>(bytes) / 1024.0 / 1024.0);
    return stream.str();
}

}  // namespace

YouTubeEncoder::YouTubeEncoder(std::string key, CodecSettings settings, ProgressSink* sink)
    : key_(std::move(key)), settings_(settings), sink_(sink != nullptr ? sink : &null_sink_) {}

bool YouTubeEncoder::encode(const std::filesystem::path& input_file,
                            const std::filesystem::path& output_file) {
    if (settings_.palette_colors != 16 && settings_.palette_colors != 64) {
        throw std::runtime_error("Unsupported palette size. Use 16 or 64.");
    }

    const int bits_per_symbol = bitsPerSymbol(static_cast<std::size_t>(settings_.palette_colors));

    if (!std::filesystem::exists(input_file)) {
        log("ERROR: input file not found: " + input_file.string());
        return false;
    }

    const auto data = readBinaryFile(input_file);
    const auto encrypted_data = xorWithKey(data, key_);

    const std::string header =
        "FILE:" + input_file.filename().string() + ":SIZE:" + std::to_string(data.size()) + "|";
    const std::vector<std::uint8_t> header_bytes(header.begin(), header.end());

    auto all_symbols = bytesToSymbols(header_bytes, bits_per_symbol);
    const auto encrypted_symbols = bytesToSymbols(encrypted_data, bits_per_symbol);
    const auto eof_symbols = bytesToSymbols(eofMarkerBytes(), bits_per_symbol);
    all_symbols.insert(all_symbols.end(), encrypted_symbols.begin(), encrypted_symbols.end());
    all_symbols.insert(all_symbols.end(), eof_symbols.begin(), eof_symbols.end());

    const auto blocks_per_region = settings_.blocksPerRegion();
    const auto frame_payload_count =
        static_cast<std::size_t>(std::ceil(static_cast<double>(all_symbols.size()) /
                                           static_cast<double>(blocks_per_region)));
    const auto frames_needed = frame_payload_count + static_cast<std::size_t>(settings_.protective_frames);

    log("============================================================");
    log("YouTube Encoder");
    log("============================================================");
    log("Grid: " + std::to_string(settings_.blocksX()) + " x " + std::to_string(settings_.blocksY()));
    log("Palette: " + std::to_string(settings_.palette_colors) + " colors (" +
        std::to_string(bits_per_symbol) + " bits per block)");
    log("FPS: " + std::to_string(settings_.fps));
    log("Encryption: " + std::string(key_.empty() ? "OFF" : "ON"));
    log("Input: " + input_file.string());
    log("Input bytes: " + std::to_string(data.size()));
    log("Header: " + header);
    log("Total blocks: " + std::to_string(all_symbols.size()));
    log("EOF blocks: " + std::to_string(eof_symbols.size()));
    log("Frames: " + std::to_string(frames_needed));

    if (output_file.has_parent_path()) {
        std::filesystem::create_directories(output_file.parent_path());
    }

    const std::string command =
        "ffmpeg -loglevel error -y -f rawvideo -pixel_format bgr24 -video_size " +
        std::to_string(settings_.width) + "x" + std::to_string(settings_.height) +
        " -framerate " + std::to_string(settings_.fps) +
        " -i - -c:v libx264 -preset slow -crf 23 -pix_fmt yuv420p -an -movflags +faststart " +
        shellEscape(output_file);

    FILE* pipe = popen(command.c_str(), "w");
    if (pipe == nullptr) {
        throw std::runtime_error("Unable to start ffmpeg for writing");
    }

    const auto frame_size =
        static_cast<std::size_t>(settings_.width) * static_cast<std::size_t>(settings_.height) * 3U;
    std::vector<std::uint8_t> frame(frame_size, 0);

    bool write_failed = false;

    for (std::size_t frame_num = 0; frame_num < frame_payload_count; ++frame_num) {
        std::fill(frame.begin(), frame.end(), 0);
        drawMarkers(frame);

        const auto start_idx = frame_num * blocks_per_region;
        const auto end_idx = std::min(start_idx + blocks_per_region, all_symbols.size());
        const auto frame_block_count = end_idx - start_idx;

        for (std::size_t index = 0; index < frame_block_count; ++index) {
            const int y = static_cast<int>(index / static_cast<std::size_t>(settings_.blocksX()));
            const int x = static_cast<int>(index % static_cast<std::size_t>(settings_.blocksX()));
            if (y < settings_.blocksY()) {
                const auto symbol = all_symbols[start_idx + index];
                drawBlock(frame,
                          x,
                          y,
                          settings_.palette_colors == 64 ? colorFrom64Symbol(symbol)
                                                         : legacyPalette()[symbol]);
            }
        }

        for (std::size_t index = 0; index < frame_block_count; ++index) {
            const int y = static_cast<int>(index / static_cast<std::size_t>(settings_.blocksX()));
            const int x = static_cast<int>(index % static_cast<std::size_t>(settings_.blocksX())) +
                          settings_.blocksX();
            if (x < settings_.blocksX() * 2 && y < settings_.blocksY()) {
                const auto symbol = all_symbols[start_idx + index];
                drawBlock(frame,
                          x,
                          y,
                          settings_.palette_colors == 64 ? colorFrom64Symbol(symbol)
                                                         : legacyPalette()[symbol]);
            }
        }

        for (std::size_t index = 0; index < frame_block_count; ++index) {
            const int y = static_cast<int>(index / static_cast<std::size_t>(settings_.blocksX())) +
                          settings_.blocksY();
            const int x = static_cast<int>(index % static_cast<std::size_t>(settings_.blocksX()));
            if (x < settings_.blocksX() && y < settings_.blocksY() * 2) {
                const auto symbol = all_symbols[start_idx + index];
                drawBlock(frame,
                          x,
                          y,
                          settings_.palette_colors == 64 ? colorFrom64Symbol(symbol)
                                                         : legacyPalette()[symbol]);
            }
        }

        if (std::fwrite(frame.data(), 1, frame.size(), pipe) != frame.size()) {
            write_failed = true;
            break;
        }

        reportProgress("encode", frame_num + 1, frames_needed);
    }

    for (int index = 0; index < settings_.protective_frames && !write_failed; ++index) {
        std::fill(frame.begin(), frame.end(), 0);
        drawMarkers(frame);

        for (int y = 0; y < settings_.blocksY() * 2; ++y) {
            for (int x = 0; x < settings_.blocksX() * 2; ++x) {
                drawBlock(frame, x, y, kBlue);
            }
        }

        if (std::fwrite(frame.data(), 1, frame.size(), pipe) != frame.size()) {
            write_failed = true;
            break;
        }

        reportProgress("protective-frames",
                       frame_payload_count + static_cast<std::size_t>(index) + 1,
                       frames_needed);
    }

    const int status = pclose(pipe);
    if (write_failed || !pipeExitedSuccessfully(status)) {
        log("ERROR: ffmpeg failed while creating the video");
        return false;
    }

    if (!std::filesystem::exists(output_file)) {
        log("ERROR: output file was not created");
        return false;
    }

    const auto size = std::filesystem::file_size(output_file);
    log("Video saved: " + output_file.string());
    log("Output bytes: " + std::to_string(size) + " (" + humanMegabytes(size) + " MB)");
    log("Duration (seconds): " +
        std::to_string(static_cast<double>(frames_needed) / static_cast<double>(settings_.fps)));
    return true;
}

void YouTubeEncoder::log(const std::string& message) const {
    sink_->log(message);
}

void YouTubeEncoder::reportProgress(const std::string& stage,
                                    std::size_t current,
                                    std::size_t total) const {
    sink_->progress(stage, current, total);
}

void YouTubeEncoder::drawMarkers(std::vector<std::uint8_t>& frame) const {
    fillRect(frame, 0, 0, settings_.marker_size, settings_.marker_size, kWhite);
    fillRect(frame,
             settings_.width - settings_.marker_size,
             0,
             settings_.width,
             settings_.marker_size,
             kWhite);
    fillRect(frame,
             0,
             settings_.height - settings_.marker_size,
             settings_.marker_size,
             settings_.height,
             kWhite);
    fillRect(frame,
             settings_.width - settings_.marker_size,
             settings_.height - settings_.marker_size,
             settings_.width,
             settings_.height,
             kWhite);

    strokeRect(frame, 0, 0, settings_.marker_size, settings_.marker_size, kBlack, 2);
    strokeRect(frame,
               settings_.width - settings_.marker_size,
               0,
               settings_.width,
               settings_.marker_size,
               kBlack,
               2);
    strokeRect(frame,
               0,
               settings_.height - settings_.marker_size,
               settings_.marker_size,
               settings_.height,
               kBlack,
               2);
    strokeRect(frame,
               settings_.width - settings_.marker_size,
               settings_.height - settings_.marker_size,
               settings_.width,
               settings_.height,
               kBlack,
               2);
}

bool YouTubeEncoder::drawBlock(std::vector<std::uint8_t>& frame, int x, int y, const Color& color) const {
    const int x1 = settings_.marker_size + x * (settings_.block_width + settings_.spacing);
    const int y1 = settings_.marker_size + y * (settings_.block_height + settings_.spacing);
    const int x2 = x1 + settings_.block_width;
    const int y2 = y1 + settings_.block_height;

    if (x2 > settings_.width - settings_.marker_size ||
        y2 > settings_.height - settings_.marker_size) {
        return false;
    }

    fillRect(frame, x1, y1, x2, y2, color);
    strokeRect(frame, x1, y1, x2, y2, kBlack, 1);
    return true;
}

void YouTubeEncoder::fillRect(std::vector<std::uint8_t>& frame,
                              int x1,
                              int y1,
                              int x2,
                              int y2,
                              const Color& color) const {
    const int min_x = std::max(0, std::min(x1, x2));
    const int max_x = std::min(settings_.width - 1, std::max(x1, x2));
    const int min_y = std::max(0, std::min(y1, y2));
    const int max_y = std::min(settings_.height - 1, std::max(y1, y2));

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const auto offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(settings_.width) +
                 static_cast<std::size_t>(x)) *
                3U;
            frame[offset] = color.b;
            frame[offset + 1] = color.g;
            frame[offset + 2] = color.r;
        }
    }
}

void YouTubeEncoder::strokeRect(std::vector<std::uint8_t>& frame,
                                int x1,
                                int y1,
                                int x2,
                                int y2,
                                const Color& color,
                                int thickness) const {
    for (int offset = 0; offset < thickness; ++offset) {
        fillRect(frame, x1 + offset, y1 + offset, x2 - offset, y1 + offset, color);
        fillRect(frame, x1 + offset, y2 - offset, x2 - offset, y2 - offset, color);
        fillRect(frame, x1 + offset, y1 + offset, x1 + offset, y2 - offset, color);
        fillRect(frame, x2 - offset, y1 + offset, x2 - offset, y2 - offset, color);
    }
}

}  // namespace youtube_cloud
