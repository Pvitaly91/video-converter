#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace video_cutter_gui {

struct FFmpegTools {
    std::filesystem::path ffmpegPath;
    std::filesystem::path ffprobePath;

    bool IsComplete() const;
    std::wstring MissingToolsMessage() const;
    static FFmpegTools Locate();
};

struct VideoInfo {
    bool ok = false;
    std::int64_t durationMilliseconds = 0;
    int width = 0;
    int height = 0;
    std::wstring videoCodec;
    std::wstring audioCodec;
    std::wstring error;
    FFmpegTools tools;
};

class VideoDurationProbe {
public:
    VideoInfo Probe(const std::filesystem::path& inputFile) const;
};

} // namespace video_cutter_gui
