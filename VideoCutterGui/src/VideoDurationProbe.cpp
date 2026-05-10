#include "VideoDurationProbe.h"

#include "ProcessRunner.h"
#include "TimeParser.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace video_cutter_gui {
namespace {

bool IsRegularFile(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && !ec;
}

std::filesystem::path ExeDirectory() {
    std::wstring buffer(MAX_PATH, L'\0');

    while (true) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            std::error_code ec;
            return std::filesystem::current_path(ec);
        }

        if (length < buffer.size() - 1) {
            buffer.resize(length);
            return std::filesystem::path(buffer).parent_path();
        }

        buffer.resize(buffer.size() * 2);
    }
}

std::filesystem::path SearchInPath(const std::wstring& name) {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = SearchPathW(nullptr, name.c_str(), nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (length == 0) {
        return {};
    }

    if (length >= buffer.size()) {
        buffer.assign(static_cast<size_t>(length) + 1, L'\0');
        length = SearchPathW(nullptr, name.c_str(), nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (length == 0 || length >= buffer.size()) {
            return {};
        }
    }

    buffer.resize(length);
    return std::filesystem::path(buffer);
}

bool ParseDurationOutput(const std::wstring& output, std::int64_t& milliseconds) {
    try {
        const long double seconds = std::stold(Trim(output));
        if (seconds <= 0) {
            return false;
        }

        milliseconds = static_cast<std::int64_t>(seconds * 1000.0L + 0.5L);
        return milliseconds > 0;
    } catch (...) {
        return false;
    }
}

void ParseKeyValueLines(const std::wstring& output, VideoInfo& info) {
    std::wstring current;
    for (wchar_t ch : output) {
        if (ch == L'\r') {
            continue;
        }

        if (ch != L'\n') {
            current.push_back(ch);
            continue;
        }

        const size_t eq = current.find(L'=');
        if (eq != std::wstring::npos) {
            const std::wstring key = current.substr(0, eq);
            const std::wstring value = current.substr(eq + 1);

            if (key == L"codec_name") {
                info.videoCodec = value;
            } else if (key == L"width") {
                info.width = std::wcstol(value.c_str(), nullptr, 10);
            } else if (key == L"height") {
                info.height = std::wcstol(value.c_str(), nullptr, 10);
            }
        }

        current.clear();
    }

    if (!current.empty()) {
        const size_t eq = current.find(L'=');
        if (eq != std::wstring::npos) {
            const std::wstring key = current.substr(0, eq);
            const std::wstring value = current.substr(eq + 1);
            if (key == L"codec_name") {
                info.videoCodec = value;
            } else if (key == L"width") {
                info.width = std::wcstol(value.c_str(), nullptr, 10);
            } else if (key == L"height") {
                info.height = std::wcstol(value.c_str(), nullptr, 10);
            }
        }
    }
}

std::wstring FirstNonEmptyLine(const std::wstring& output) {
    std::wstring current;
    for (wchar_t ch : output) {
        if (ch == L'\r') {
            continue;
        }
        if (ch == L'\n') {
            current = Trim(current);
            if (!current.empty()) {
                return current;
            }
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    return Trim(current);
}

std::wstring ProcessFailureMessage(const std::wstring& action, const ProcessResult& result) {
    if (!result.started) {
        return action + L" failed to start. " + result.error;
    }

    std::wstring output = Trim(result.output);
    if (output.empty()) {
        output = L"exit code " + std::to_wstring(result.exitCode);
    }
    return action + L" failed. " + output;
}

} // namespace

bool FFmpegTools::IsComplete() const {
    return !ffmpegPath.empty() && !ffprobePath.empty();
}

std::wstring FFmpegTools::MissingToolsMessage() const {
    return L"FFmpeg not found. Install FFmpeg or place ffmpeg.exe and ffprobe.exe into tools\\ffmpeg\\bin.";
}

FFmpegTools FFmpegTools::Locate() {
    FFmpegTools tools;

    const std::filesystem::path pathFfmpeg = SearchInPath(L"ffmpeg.exe");
    const std::filesystem::path pathFfprobe = SearchInPath(L"ffprobe.exe");

    if (IsRegularFile(pathFfmpeg)) {
        tools.ffmpegPath = pathFfmpeg;
    }
    if (IsRegularFile(pathFfprobe)) {
        tools.ffprobePath = pathFfprobe;
    }

    if (tools.IsComplete()) {
        return tools;
    }

    const std::filesystem::path localBin = ExeDirectory() / L"tools" / L"ffmpeg" / L"bin";
    const std::filesystem::path localFfmpeg = localBin / L"ffmpeg.exe";
    const std::filesystem::path localFfprobe = localBin / L"ffprobe.exe";

    if (tools.ffmpegPath.empty() && IsRegularFile(localFfmpeg)) {
        tools.ffmpegPath = localFfmpeg;
    }
    if (tools.ffprobePath.empty() && IsRegularFile(localFfprobe)) {
        tools.ffprobePath = localFfprobe;
    }

    return tools;
}

VideoInfo VideoDurationProbe::Probe(const std::filesystem::path& inputFile) const {
    VideoInfo info;
    info.tools = FFmpegTools::Locate();
    if (!info.tools.IsComplete()) {
        info.error = info.tools.MissingToolsMessage();
        return info;
    }

    ProcessRunner runner(nullptr);

    const std::vector<std::wstring> durationArgs = {
        L"-v", L"error",
        L"-show_entries", L"format=duration",
        L"-of", L"default=noprint_wrappers=1:nokey=1",
        inputFile.wstring()
    };

    const ProcessResult durationResult = runner.Run(info.tools.ffprobePath, durationArgs);
    if (!durationResult.started || durationResult.exitCode != 0) {
        info.error = ProcessFailureMessage(L"ffprobe duration", durationResult);
        return info;
    }

    if (!ParseDurationOutput(durationResult.output, info.durationMilliseconds)) {
        info.error = L"ffprobe returned an invalid duration: " + Trim(durationResult.output);
        return info;
    }

    const std::vector<std::wstring> videoArgs = {
        L"-v", L"error",
        L"-select_streams", L"v:0",
        L"-show_entries", L"stream=codec_name,width,height",
        L"-of", L"default=noprint_wrappers=1",
        inputFile.wstring()
    };

    const ProcessResult videoResult = runner.Run(info.tools.ffprobePath, videoArgs);
    if (!videoResult.started || videoResult.exitCode != 0) {
        info.error = ProcessFailureMessage(L"ffprobe video stream", videoResult);
        return info;
    }

    ParseKeyValueLines(videoResult.output, info);
    if (info.videoCodec.empty()) {
        info.error = L"ffprobe did not find a video stream.";
        return info;
    }

    const std::vector<std::wstring> audioArgs = {
        L"-v", L"error",
        L"-select_streams", L"a:0",
        L"-show_entries", L"stream=codec_name",
        L"-of", L"default=noprint_wrappers=1:nokey=1",
        inputFile.wstring()
    };

    const ProcessResult audioResult = runner.Run(info.tools.ffprobePath, audioArgs);
    if (audioResult.started && audioResult.exitCode == 0) {
        info.audioCodec = FirstNonEmptyLine(audioResult.output);
    }

    if (info.audioCodec.empty()) {
        info.audioCodec = L"none";
    }

    info.ok = true;
    return info;
}

} // namespace video_cutter_gui
