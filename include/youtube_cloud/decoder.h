#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "youtube_cloud/common.h"
#include "youtube_cloud/progress_sink.h"

namespace youtube_cloud {

class YouTubeDecoder {
public:
    explicit YouTubeDecoder(std::string key = {},
                            CodecSettings settings = {},
                            ProgressSink* sink = nullptr);

    bool decode(const std::filesystem::path& video_file,
                const std::filesystem::path& output_dir = ".");

private:
    void log(const std::string& message) const;
    void reportProgress(const std::string& stage, std::size_t current, std::size_t total) const;
    void precomputeCoordinates();
    std::vector<std::uint32_t> sampleFrame(const std::vector<std::uint8_t>& frame) const;
    std::uint8_t packedColorToLegacySymbol(std::uint32_t packed_color);
    std::uint8_t packedColorToExtendedSymbol(std::uint32_t packed_color);
    bool recoverFileFromSamples(const std::vector<std::uint32_t>& sampled_colors,
                                const std::filesystem::path& output_dir,
                                int bits_per_symbol,
                                bool legacy_mode,
                                bool write_raw_on_failure);
    VideoStreamInfo probeVideo(const std::filesystem::path& video_file) const;

    std::string key_;
    CodecSettings settings_;
    ProgressSink* sink_;
    NullProgressSink null_sink_;
    std::vector<std::pair<int, int>> block_coords_;
    std::unordered_map<std::uint32_t, std::uint8_t> color_cache_;
    std::size_t cache_hits_ = 0;
    std::size_t cache_misses_ = 0;
};

}  // namespace youtube_cloud
