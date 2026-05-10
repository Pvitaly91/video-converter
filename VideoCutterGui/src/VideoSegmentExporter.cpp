#include "VideoSegmentExporter.h"

#include "OutputPathBuilder.h"
#include "ProcessRunner.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace video_cutter_gui {
namespace {

std::wstring TimestampId() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t raw = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &raw);

    std::wostringstream stream;
    stream << std::put_time(&local, L"%Y%m%d_%H%M%S");
    return stream.str();
}

std::filesystem::path BuildTempDirectory(const std::filesystem::path& outputFile) {
    return outputFile.parent_path() / (L".video_cutter_temp_" + TimestampId());
}

std::filesystem::path BuildPartPath(const std::filesystem::path& tempDirectory, size_t index) {
    std::wostringstream name;
    name << L"part_" << std::setfill(L'0') << std::setw(3) << (index + 1) << L".mp4";
    return tempDirectory / name.str();
}

std::wstring TrimProcessOutput(std::wstring output) {
    while (!output.empty() && (output.back() == L'\r' || output.back() == L'\n')) {
        output.pop_back();
    }
    return output;
}

std::wstring ToolOrFallback(const std::filesystem::path& tool, const wchar_t* fallback) {
    return tool.empty() ? std::wstring(fallback) : tool.wstring();
}

std::wstring BuildReadableCommand(const std::filesystem::path& executable,
                                  const std::vector<std::wstring>& arguments) {
    return BuildCommandLine(executable, arguments);
}

bool WriteConcatList(const std::filesystem::path& concatList, size_t partCount, std::wstring& error) {
    std::ofstream stream(concatList, std::ios::binary | std::ios::trunc);
    if (!stream) {
        error = L"Cannot write concat list: " + concatList.wstring();
        return false;
    }

    for (size_t index = 0; index < partCount; ++index) {
        std::ostringstream name;
        name << "file 'part_" << std::setfill('0') << std::setw(3) << (index + 1) << ".mp4'\n";
        stream << name.str();
    }

    return true;
}

bool ParsePositiveDurationFromProbe(const std::wstring& output) {
    std::wstring line;
    for (wchar_t ch : output) {
        if (ch == L'\r') {
            continue;
        }

        if (ch != L'\n') {
            line.push_back(ch);
            continue;
        }

        const std::wstring trimmed = Trim(line);
        if (trimmed.rfind(L"duration=", 0) == 0) {
            try {
                return std::stold(trimmed.substr(9)) > 0.0L;
            } catch (...) {
                return false;
            }
        }

        line.clear();
    }

    line = Trim(line);
    if (line.rfind(L"duration=", 0) == 0) {
        try {
            return std::stold(line.substr(9)) > 0.0L;
        } catch (...) {
            return false;
        }
    }

    return false;
}

bool ContainsVideoStream(const std::wstring& output) {
    return output.find(L"codec_type=video") != std::wstring::npos
        || output.find(L"codec_name=") != std::wstring::npos;
}

std::wstring ProcessErrorText(const std::wstring& title, const ProcessResult& result) {
    if (result.canceled) {
        return L"Canceled.";
    }

    if (!result.started) {
        return title + L" did not start. " + result.error;
    }

    std::wstring output = TrimProcessOutput(result.output);
    if (output.empty()) {
        output = L"exit code " + std::to_wstring(result.exitCode);
    }

    return title + L" failed. " + output;
}

std::wstring ErrorCodeMessage(const std::error_code& ec) {
    return BytesToWide(ec.message());
}

} // namespace

std::vector<std::wstring> BuildCutArguments(const std::filesystem::path& inputFile,
                                            const SegmentInterval& segment,
                                            const std::filesystem::path& partFile) {
    return {
        L"-hide_banner",
        L"-nostdin",
        L"-y",
        L"-ss", segment.startText,
        L"-to", segment.endText,
        L"-i", inputFile.wstring(),
        L"-map", L"0:v:0",
        L"-map", L"0:a?",
        L"-c", L"copy",
        L"-avoid_negative_ts", L"make_zero",
        L"-reset_timestamps", L"1",
        partFile.wstring()
    };
}

std::vector<std::wstring> BuildConcatArguments(const std::filesystem::path& concatList,
                                               const std::filesystem::path& outputFile) {
    return {
        L"-hide_banner",
        L"-nostdin",
        L"-y",
        L"-f", L"concat",
        L"-safe", L"0",
        L"-i", concatList.wstring(),
        L"-c", L"copy",
        L"-movflags", L"+faststart",
        outputFile.wstring()
    };
}

VideoSegmentExporter::VideoSegmentExporter(LogCallback logCallback, ProgressCallback progressCallback)
    : logCallback_(std::move(logCallback)),
      progressCallback_(std::move(progressCallback)) {
}

VideoSegmentExporter::~VideoSegmentExporter() {
    Cancel();
}

void VideoSegmentExporter::Cancel() {
    cancelRequested_.store(true);

    std::lock_guard<std::mutex> lock(activeProcessMutex_);
    if (activeProcess_ != nullptr) {
        TerminateProcess(activeProcess_, 1);
    }
}

void VideoSegmentExporter::Log(const std::wstring& text) const {
    if (logCallback_) {
        logCallback_(text);
    }
}

void VideoSegmentExporter::Progress(int percent, const std::wstring& message) const {
    if (progressCallback_) {
        progressCallback_(ExportProgress{ percent, message });
    }
}

void VideoSegmentExporter::SetActiveProcess(HANDLE process) {
    std::lock_guard<std::mutex> lock(activeProcessMutex_);
    activeProcess_ = process;
}

void VideoSegmentExporter::ClearActiveProcess() {
    std::lock_guard<std::mutex> lock(activeProcessMutex_);
    activeProcess_ = nullptr;
}

bool VideoSegmentExporter::ValidateOutput(const FFmpegTools& tools,
                                          const std::filesystem::path& outputFile,
                                          std::wstring& error) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(outputFile, ec) || ec) {
        error = L"Output file was not created.";
        return false;
    }

    const std::uintmax_t size = std::filesystem::file_size(outputFile, ec);
    if (ec || size == 0) {
        error = L"Output file is empty.";
        return false;
    }

    ProcessRunner runner(&cancelRequested_);
    const std::vector<std::wstring> args = {
        L"-v", L"error",
        L"-select_streams", L"v:0",
        L"-show_entries", L"stream=codec_type,codec_name:format=duration",
        L"-of", L"default=noprint_wrappers=1",
        outputFile.wstring()
    };

    const ProcessResult result = runner.Run(tools.ffprobePath, args);
    if (result.canceled) {
        error = L"Validation canceled.";
        return false;
    }

    if (!result.started || result.exitCode != 0) {
        error = ProcessErrorText(L"ffprobe validation", result);
        return false;
    }

    if (!ContainsVideoStream(result.output)) {
        error = L"Validation failed: output has no video stream.";
        return false;
    }

    if (!ParsePositiveDurationFromProbe(result.output)) {
        error = L"Validation failed: output duration is not greater than zero.";
        return false;
    }

    return true;
}

ExportResult VideoSegmentExporter::Export(const ExportSettings& settings) {
    cancelRequested_.store(false);

    ExportResult result;
    result.dryRun = settings.dryRun;
    result.outputFile = settings.outputFile;
    result.tempDirectory = BuildTempDirectory(settings.outputFile);

    const FFmpegTools tools = FFmpegTools::Locate();
    const std::filesystem::path ffmpegExecutable = tools.ffmpegPath.empty()
        ? std::filesystem::path(L"ffmpeg.exe")
        : tools.ffmpegPath;

    Log(L"FFmpeg:  " + ToolOrFallback(tools.ffmpegPath, L"ffmpeg.exe"));
    Log(L"FFprobe: " + ToolOrFallback(tools.ffprobePath, L"ffprobe.exe"));
    Log(L"Input:   " + settings.inputFile.wstring());
    Log(L"Output:  " + settings.outputFile.wstring());
    Log(L"Temp:    " + result.tempDirectory.wstring());
    Log(L"Intervals: " + std::to_wstring(settings.segments.size()));
    Log(L"Fast mode: -c copy. Cuts can be keyframe-aligned in MP4/H.264.");
    Log(L"");

    const std::filesystem::path concatList = result.tempDirectory / L"concat_list.txt";
    std::vector<std::filesystem::path> partFiles;
    partFiles.reserve(settings.segments.size());
    for (size_t index = 0; index < settings.segments.size(); ++index) {
        partFiles.push_back(BuildPartPath(result.tempDirectory, index));
    }

    for (size_t index = 0; index < settings.segments.size(); ++index) {
        const std::vector<std::wstring> args = BuildCutArguments(settings.inputFile, settings.segments[index], partFiles[index]);
        Log(L"Segment " + std::to_wstring(index + 1) + L"/" + std::to_wstring(settings.segments.size()) + L": "
            + settings.segments[index].startText + L" - " + settings.segments[index].endText);
        Log(BuildReadableCommand(ffmpegExecutable, args));
        Log(L"");
    }

    Log(L"Concat:");
    Log(BuildReadableCommand(ffmpegExecutable, BuildConcatArguments(concatList, settings.outputFile)));
    Log(L"");

    if (settings.dryRun) {
        Progress(100, L"Dry run complete");
        result.success = true;
        return result;
    }

    if (!tools.IsComplete()) {
        result.error = tools.MissingToolsMessage();
        Log(result.error);
        return result;
    }

    std::error_code ec;
    std::filesystem::create_directories(settings.outputFile.parent_path(), ec);
    if (ec) {
        result.error = L"Cannot create output folder: " + ErrorCodeMessage(ec);
        Log(result.error);
        return result;
    }

    std::filesystem::create_directories(result.tempDirectory, ec);
    if (ec) {
        result.error = L"Cannot create temp folder: " + ErrorCodeMessage(ec);
        Log(result.error);
        return result;
    }

    ProcessRunner runner(
        &cancelRequested_,
        [this](HANDLE process) { SetActiveProcess(process); },
        [this]() { ClearActiveProcess(); });

    for (size_t index = 0; index < settings.segments.size(); ++index) {
        if (cancelRequested_.load()) {
            result.canceled = true;
            result.error = L"Canceled.";
            Log(result.error);
            return result;
        }

        const int percent = static_cast<int>((index * 70) / std::max<size_t>(1, settings.segments.size()));
        Progress(percent, L"creating segment " + std::to_wstring(index + 1) + L"/" + std::to_wstring(settings.segments.size()));
        Log(L"Creating segment " + std::to_wstring(index + 1) + L"/" + std::to_wstring(settings.segments.size()) + L"...");

        const ProcessResult processResult = runner.Run(tools.ffmpegPath, BuildCutArguments(settings.inputFile, settings.segments[index], partFiles[index]));
        if (processResult.canceled) {
            result.canceled = true;
            result.error = L"Canceled.";
            Log(result.error);
            return result;
        }

        if (!processResult.started || processResult.exitCode != 0) {
            result.error = ProcessErrorText(L"ffmpeg segment " + std::to_wstring(index + 1), processResult);
            Log(result.error);
            return result;
        }

        const std::wstring output = TrimProcessOutput(processResult.output);
        if (!output.empty()) {
            Log(output);
        }
    }

    std::wstring writeError;
    if (!WriteConcatList(concatList, partFiles.size(), writeError)) {
        result.error = writeError;
        Log(result.error);
        return result;
    }

    Progress(76, L"concat");
    Log(L"Concatenating segments...");
    const ProcessResult concatResult = runner.Run(tools.ffmpegPath, BuildConcatArguments(concatList, settings.outputFile));
    if (concatResult.canceled) {
        result.canceled = true;
        result.error = L"Canceled.";
        Log(result.error);
        return result;
    }

    if (!concatResult.started || concatResult.exitCode != 0) {
        result.error = ProcessErrorText(L"ffmpeg concat", concatResult);
        Log(result.error);
        return result;
    }

    const std::wstring concatOutput = TrimProcessOutput(concatResult.output);
    if (!concatOutput.empty()) {
        Log(concatOutput);
    }

    Progress(90, L"validation");
    Log(L"Validating output with ffprobe...");
    std::wstring validationError;
    if (!ValidateOutput(tools, settings.outputFile, validationError)) {
        result.error = validationError;
        Log(result.error);
        return result;
    }

    if (!settings.keepTemp) {
        std::filesystem::remove_all(result.tempDirectory, ec);
        if (ec) {
            result.error = L"Output is valid, but temp folder could not be removed: "
                + ErrorCodeMessage(ec);
            Log(result.error);
            return result;
        }
    }

    Progress(100, L"done");
    Log(L"Done.");
    result.success = true;
    return result;
}

} // namespace video_cutter_gui
