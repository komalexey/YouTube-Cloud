#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "youtube_cloud/common.h"
#include "youtube_cloud/decoder.h"
#include "youtube_cloud/encoder.h"
#include "youtube_cloud/progress_sink.h"

namespace youtube_cloud {

class ConsoleProgressSink final : public ProgressSink {
public:
    void log(const std::string& message) override {
        std::cout << message << '\n';
    }

    void progress(const std::string& stage, std::size_t current, std::size_t total) override {
        if (total == 0) {
            return;
        }
        if (current == total || current == 1 || current % 25 == 0) {
            std::cout << "[" << stage << "] " << current << "/" << total << '\n';
        }
    }
};

void printUsage() {
    std::cout << "============================================================\n";
    std::cout << "YouTube File Storage (C++)\n";
    std::cout << "============================================================\n\n";
    std::cout << "Usage:\n";
    std::cout << "  youtube_cloud encode <file> [output.mp4]\n";
    std::cout << "  youtube_cloud -e <file> [output.mp4]\n";
    std::cout << "  youtube_cloud decode <video> [output_dir]\n";
    std::cout << "  youtube_cloud -d <video> [output_dir]\n\n";
    std::cout << "Features:\n";
    std::cout << "  * 6 FPS\n";
    std::cout << "  * Scale decode input to 1920x1080\n";
    std::cout << "  * EOF marker\n";
    std::cout << "  * 5 protective frames\n";
    std::cout << "  * Optional XOR key from key.txt\n";
}

}  // namespace youtube_cloud

int main(int argc, char** argv) {
    using namespace youtube_cloud;

    if (argc < 2) {
        printUsage();
        return EXIT_SUCCESS;
    }

    ConsoleProgressSink sink;
    const auto key = readKeyFromFile("key.txt");
    if (key.empty()) {
        sink.log("INFO: key.txt not found or empty, encryption is disabled");
    } else {
        sink.log("INFO: encryption key loaded from key.txt");
    }

    const std::string command = argv[1];

    try {
        if (command == "encode" || command == "-e") {
            if (argc < 3) {
                printUsage();
                return EXIT_FAILURE;
            }

            const std::filesystem::path input_file = argv[2];
            const std::filesystem::path output_file = argc > 3 ? argv[3] : input_file.stem().string() + ".mp4";
            YouTubeEncoder encoder(key, {}, &sink);
            return encoder.encode(input_file, output_file) ? EXIT_SUCCESS : EXIT_FAILURE;
        }

        if (command == "decode" || command == "-d") {
            if (argc < 3) {
                printUsage();
                return EXIT_FAILURE;
            }

            const std::filesystem::path video_file = argv[2];
            const std::filesystem::path output_dir = argc > 3 ? argv[3] : ".";
            YouTubeDecoder decoder(key, {}, &sink);
            return decoder.decode(video_file, output_dir) ? EXIT_SUCCESS : EXIT_FAILURE;
        }

        sink.log("ERROR: unknown command: " + command);
        printUsage();
        return EXIT_FAILURE;
    } catch (const std::exception& error) {
        sink.log(std::string("ERROR: ") + error.what());
        return EXIT_FAILURE;
    }
}
