#pragma once

#include "Config.h"
#include "FFmpegRunner.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace video_optimizer {

enum class FileStatus {
    Ok,
    Skipped,
    Error,
    BiggerThanOriginal
};

struct FileResult {
    std::filesystem::path input;
    std::filesystem::path output;
    std::uintmax_t sizeBefore = 0;
    std::uintmax_t sizeAfter = 0;
    FileStatus status = FileStatus::Skipped;
    std::wstring message;
};

struct Summary {
    size_t found = 0;
    size_t processed = 0;
    size_t skipped = 0;
    size_t errors = 0;
    size_t biggerThanOriginal = 0;
    std::uintmax_t totalBefore = 0;
    std::uintmax_t totalAfter = 0;
};

class VideoOptimizer {
public:
    explicit VideoOptimizer(Config config);

    int Run();

private:
    FileResult ProcessFile(const std::filesystem::path& input, const FFmpegRunner& runner) const;

    std::filesystem::path BuildOutputPath(const std::filesystem::path& input) const;
    std::filesystem::path MakeUniqueOutputPath(const std::filesystem::path& preferred) const;
    void PrintFileResult(const FileResult& result) const;
    void AddToSummary(const FileResult& result, Summary& summary) const;
    void PrintSummary(const Summary& summary) const;

    Config config_;
};

} // namespace video_optimizer
