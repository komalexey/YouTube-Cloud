#pragma once

#include <cstddef>
#include <string>

namespace youtube_cloud {

class ProgressSink {
public:
    virtual ~ProgressSink() = default;

    virtual void log(const std::string& message) = 0;
    virtual void progress(const std::string& stage, std::size_t current, std::size_t total) = 0;
};

class NullProgressSink final : public ProgressSink {
public:
    void log(const std::string&) override {}
    void progress(const std::string&, std::size_t, std::size_t) override {}
};

}  // namespace youtube_cloud
