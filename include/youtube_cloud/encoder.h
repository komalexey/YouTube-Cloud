#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "youtube_cloud/common.h"
#include "youtube_cloud/progress_sink.h"

namespace youtube_cloud {

class YouTubeEncoder {
public:
    explicit YouTubeEncoder(std::string key = {},
                            CodecSettings settings = {},
                            ProgressSink* sink = nullptr);

    bool encode(const std::filesystem::path& input_file,
                const std::filesystem::path& output_file);

private:
    void log(const std::string& message) const;
    void reportProgress(const std::string& stage, std::size_t current, std::size_t total) const;

    void drawMarkers(std::vector<std::uint8_t>& frame) const;
    bool drawBlock(std::vector<std::uint8_t>& frame, int x, int y, const Color& color) const;
    void fillRect(std::vector<std::uint8_t>& frame,
                  int x1,
                  int y1,
                  int x2,
                  int y2,
                  const Color& color) const;
    void strokeRect(std::vector<std::uint8_t>& frame,
                    int x1,
                    int y1,
                    int x2,
                    int y2,
                    const Color& color,
                    int thickness) const;

    std::string key_;
    CodecSettings settings_;
    ProgressSink* sink_;
    NullProgressSink null_sink_;
};

}  // namespace youtube_cloud
