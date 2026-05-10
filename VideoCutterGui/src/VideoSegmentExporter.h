#pragma once

#include "TimeParser.h"
#include "VideoDurationProbe.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace video_cutter_gui {

struct ExportSettings {
    std::filesystem::path inputFile;
    std::filesystem::path outputFile;
    std::vector<SegmentInterval> segments;
    bool dryRun = false;
    bool keepTemp = false;
};

struct ExportProgress {
    int percent = 0;
    std::wstring message;
};

struct ExportResult {
    bool success = false;
    bool canceled = false;
    bool dryRun = false;
    std::filesystem::path outputFile;
    std::filesystem::path tempDirectory;
    std::wstring error;
};

class VideoSegmentExporter {
public:
    using LogCallback = std::function<void(const std::wstring&)>;
    using ProgressCallback = std::function<void(const ExportProgress&)>;

    VideoSegmentExporter(LogCallback logCallback, ProgressCallback progressCallback);
    ~VideoSegmentExporter();

    ExportResult Export(const ExportSettings& settings);
    void Cancel();

private:
    void Log(const std::wstring& text) const;
    void Progress(int percent, const std::wstring& message) const;
    void SetActiveProcess(HANDLE process);
    void ClearActiveProcess();
    bool ValidateOutput(const FFmpegTools& tools,
                        const std::filesystem::path& outputFile,
                        std::wstring& error);

    std::atomic_bool cancelRequested_{false};
    mutable std::mutex activeProcessMutex_;
    HANDLE activeProcess_ = nullptr;
    LogCallback logCallback_;
    ProgressCallback progressCallback_;
};

std::vector<std::wstring> BuildCutArguments(const std::filesystem::path& inputFile,
                                            const SegmentInterval& segment,
                                            const std::filesystem::path& partFile);

std::vector<std::wstring> BuildConcatArguments(const std::filesystem::path& concatList,
                                               const std::filesystem::path& outputFile);

} // namespace video_cutter_gui
