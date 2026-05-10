#pragma once

#include "FFmpegRunner.h"

#include <filesystem>
#include <string>

namespace video_optimizer {

struct ProbeResult {
    bool ok = false;
    std::wstring error;
};

class VideoProbe {
public:
    explicit VideoProbe(const FFmpegRunner& runner);

    ProbeResult Probe(const std::filesystem::path& input) const;

private:
    const FFmpegRunner& runner_;
};

} // namespace video_optimizer
