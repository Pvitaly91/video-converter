#include "VideoProbe.h"

#include <algorithm>
#include <cwctype>
#include <string>

namespace video_optimizer {
namespace {

std::wstring Trim(std::wstring value) {
    auto isSpace = [](wchar_t ch) {
        return std::iswspace(ch) != 0;
    };

    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), isSpace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), isSpace).base(), value.end());
    return value;
}

} // namespace

VideoProbe::VideoProbe(const FFmpegRunner& runner)
    : runner_(runner) {
}

ProbeResult VideoProbe::Probe(const std::filesystem::path& input) const {
    ProbeResult probe;
    const ProcessResult result = runner_.RunFFprobe(input);

    if (!result.started) {
        probe.error = result.error;
        return probe;
    }

    if (result.exitCode != 0) {
        probe.error = L"ffprobe failed";
        const std::wstring output = Trim(result.output);
        if (!output.empty()) {
            probe.error += L": " + output;
        }
        return probe;
    }

    if (result.output.find(L"codec_name=") == std::wstring::npos) {
        probe.error = L"ffprobe did not find a video stream.";
        return probe;
    }

    probe.ok = true;
    return probe;
}

} // namespace video_optimizer
