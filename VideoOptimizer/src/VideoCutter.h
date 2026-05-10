#pragma once

#include "Config.h"
#include "FFmpegRunner.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace video_optimizer {

class VideoCutter {
public:
    explicit VideoCutter(CutConfig config);

    int Run();

private:
    std::filesystem::path CreateTempDirectoryPath() const;
    std::filesystem::path BuildPartPath(const std::filesystem::path& tempDirectory, size_t index) const;
    std::vector<std::wstring> BuildCutArguments(const Segment& segment,
                                                const std::filesystem::path& partFile) const;
    std::vector<std::wstring> BuildConcatArguments(const std::filesystem::path& listFile) const;

    bool PrepareOutputDirectory(std::wstring& error) const;
    bool RunPartExtraction(const FFmpegRunner& runner,
                           const std::filesystem::path& tempDirectory,
                           std::vector<std::filesystem::path>& partFiles,
                           std::wstring& error) const;
    bool RunConcat(const FFmpegRunner& runner,
                   const std::filesystem::path& listFile,
                   std::wstring& error) const;
    void PrintDryRun(const FFmpegRunner& runner,
                     const std::filesystem::path& tempDirectory,
                     const std::vector<std::filesystem::path>& partFiles,
                     const std::filesystem::path& listFile) const;
    void PrintSummary(bool ok,
                      const std::filesystem::path& tempDirectory,
                      bool tempCreated,
                      bool tempDeleted,
                      const std::wstring& error) const;

    std::int64_t TotalDurationMilliseconds() const;

    CutConfig config_;
};

} // namespace video_optimizer
