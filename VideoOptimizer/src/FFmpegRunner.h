#pragma once

#include "Config.h"

#include <filesystem>
#include <string>
#include <vector>

namespace video_optimizer {

struct ProcessResult {
    bool started = false;
    unsigned long exitCode = 1;
    std::wstring output;
    std::wstring error;
};

class FFmpegRunner {
public:
    bool Locate();
    bool IsAvailable() const;

    const std::filesystem::path& FfmpegPath() const;
    const std::filesystem::path& FfprobePath() const;

    std::wstring MissingToolsMessage() const;
    std::wstring BuildFFmpegCommand(const std::filesystem::path& input,
                                    const std::filesystem::path& output,
                                    const Config& config) const;

    ProcessResult RunFFprobe(const std::filesystem::path& input) const;
    ProcessResult RunFFmpeg(const std::filesystem::path& input,
                            const std::filesystem::path& output,
                            const Config& config) const;

private:
    ProcessResult RunProcess(const std::filesystem::path& executable,
                             const std::vector<std::wstring>& arguments) const;

    std::filesystem::path ffmpegPath_;
    std::filesystem::path ffprobePath_;
};

} // namespace video_optimizer
