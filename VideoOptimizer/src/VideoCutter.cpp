#include "VideoCutter.h"

#include "ConcatListWriter.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

namespace video_optimizer {
namespace {

std::wstring Widen(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}

std::wstring FormatMegabytes(std::uintmax_t bytes) {
    std::wostringstream stream;
    stream << std::fixed << std::setprecision(2) << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << L" MB";
    return stream.str();
}

std::wstring TrimProcessOutput(std::wstring value) {
    while (!value.empty() && (value.back() == L'\r' || value.back() == L'\n')) {
        value.pop_back();
    }
    return value;
}

} // namespace

VideoCutter::VideoCutter(CutConfig config)
    : config_(std::move(config)) {
}

int VideoCutter::Run() {
    FFmpegRunner runner;
    const bool ffmpegFound = runner.Locate();

    if (!ffmpegFound) {
        if (!config_.dryRun) {
            std::wcerr << runner.MissingToolsMessage() << L'\n';
            return 1;
        }
        std::wcout << runner.MissingToolsMessage() << L'\n'
                   << L"Dry-run mode will still print command previews using ffmpeg.exe.\n\n";
    }

    const std::filesystem::path tempDirectory = CreateTempDirectoryPath();
    std::vector<std::filesystem::path> partFiles;
    for (size_t index = 0; index < config_.segments.size(); ++index) {
        partFiles.push_back(BuildPartPath(tempDirectory, index));
    }

    const std::filesystem::path listFile = tempDirectory / L"list.txt";

    if (config_.dryRun) {
        PrintDryRun(runner, tempDirectory, partFiles, listFile);
        PrintSummary(true, tempDirectory, false, false, L"");
        return 0;
    }

    std::wstring error;
    if (!PrepareOutputDirectory(error)) {
        PrintSummary(false, tempDirectory, false, false, error);
        return 2;
    }

    std::error_code ec;
    std::filesystem::create_directories(tempDirectory, ec);
    if (ec) {
        error = std::wstring(L"Cannot create temporary directory: ") + Widen(ec.message());
        PrintSummary(false, tempDirectory, false, false, error);
        return 2;
    }

    if (!RunPartExtraction(runner, tempDirectory, partFiles, error)) {
        PrintSummary(false, tempDirectory, true, false, error);
        return 2;
    }

    if (!ConcatListWriter::Write(listFile, partFiles, error)) {
        PrintSummary(false, tempDirectory, true, false, error);
        return 2;
    }

    if (!RunConcat(runner, listFile, error)) {
        PrintSummary(false, tempDirectory, true, false, error);
        return 2;
    }

    bool tempDeleted = false;
    if (!config_.keepTemp) {
        std::filesystem::remove_all(tempDirectory, ec);
        tempDeleted = !ec;
        if (ec) {
            error = std::wstring(L"Output created, but temporary directory could not be removed: ") + Widen(ec.message());
        }
    }

    PrintSummary(error.empty(), tempDirectory, true, tempDeleted, error);
    return error.empty() ? 0 : 2;
}

std::filesystem::path VideoCutter::CreateTempDirectoryPath() const {
    const auto ticks = std::chrono::system_clock::now().time_since_epoch().count();
    const std::filesystem::path base = config_.outputFile.has_parent_path()
        ? config_.outputFile.parent_path()
        : std::filesystem::temp_directory_path();

    return base / (L"video_optimizer_temp_" + std::to_wstring(ticks));
}

std::filesystem::path VideoCutter::BuildPartPath(const std::filesystem::path& tempDirectory, size_t index) const {
    std::wostringstream fileName;
    fileName << L"part_" << std::setfill(L'0') << std::setw(3) << (index + 1) << L".mp4";
    return tempDirectory / fileName.str();
}

std::vector<std::wstring> VideoCutter::BuildCutArguments(const Segment& segment,
                                                         const std::filesystem::path& partFile) const {
    std::vector<std::wstring> arguments;
    arguments.push_back(L"-y");
    arguments.push_back(L"-ss");
    arguments.push_back(segment.start);
    arguments.push_back(L"-to");
    arguments.push_back(segment.end);
    arguments.push_back(L"-i");
    arguments.push_back(config_.inputFile.wstring());
    arguments.push_back(L"-map");
    arguments.push_back(L"0:v:0");
    arguments.push_back(L"-map");
    arguments.push_back(L"0:a?");
    arguments.push_back(L"-c");
    arguments.push_back(L"copy");
    arguments.push_back(L"-avoid_negative_ts");
    arguments.push_back(L"make_zero");
    arguments.push_back(L"-reset_timestamps");
    arguments.push_back(L"1");
    arguments.push_back(partFile.wstring());
    return arguments;
}

std::vector<std::wstring> VideoCutter::BuildConcatArguments(const std::filesystem::path& listFile) const {
    std::vector<std::wstring> arguments;
    arguments.push_back(L"-y");
    arguments.push_back(L"-f");
    arguments.push_back(L"concat");
    arguments.push_back(L"-safe");
    arguments.push_back(L"0");
    arguments.push_back(L"-i");
    arguments.push_back(listFile.wstring());
    arguments.push_back(L"-c");
    arguments.push_back(L"copy");
    arguments.push_back(config_.outputFile.wstring());
    return arguments;
}

bool VideoCutter::PrepareOutputDirectory(std::wstring& error) const {
    std::error_code ec;
    const std::filesystem::path parent = config_.outputFile.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            error = std::wstring(L"Cannot create output directory: ") + Widen(ec.message());
            return false;
        }
    }

    return true;
}

bool VideoCutter::RunPartExtraction(const FFmpegRunner& runner,
                                    const std::filesystem::path&,
                                    std::vector<std::filesystem::path>& partFiles,
                                    std::wstring& error) const {
    for (size_t index = 0; index < config_.segments.size(); ++index) {
        std::wcout << L"Cutting segment " << (index + 1) << L"/" << config_.segments.size()
                   << L": " << config_.segments[index].start << L"-" << config_.segments[index].end << L'\n';

        const ProcessResult result = runner.RunFFmpeg(BuildCutArguments(config_.segments[index], partFiles[index]));
        if (!result.started) {
            error = result.error;
            return false;
        }

        if (result.exitCode != 0) {
            error = L"ffmpeg failed while cutting segment " + std::to_wstring(index + 1)
                + L" with exit code " + std::to_wstring(result.exitCode);
            const std::wstring output = TrimProcessOutput(result.output);
            if (!output.empty()) {
                error += L"\n" + output;
            }
            return false;
        }
    }

    return true;
}

bool VideoCutter::RunConcat(const FFmpegRunner& runner,
                            const std::filesystem::path& listFile,
                            std::wstring& error) const {
    std::wcout << L"Concatenating " << config_.segments.size() << L" parts...\n";

    const ProcessResult result = runner.RunFFmpeg(BuildConcatArguments(listFile));
    if (!result.started) {
        error = result.error;
        return false;
    }

    if (result.exitCode != 0) {
        error = L"ffmpeg failed while concatenating with exit code " + std::to_wstring(result.exitCode);
        const std::wstring output = TrimProcessOutput(result.output);
        if (!output.empty()) {
            error += L"\n" + output;
        }
        return false;
    }

    return true;
}

void VideoCutter::PrintDryRun(const FFmpegRunner& runner,
                              const std::filesystem::path& tempDirectory,
                              const std::vector<std::filesystem::path>& partFiles,
                              const std::filesystem::path& listFile) const {
    std::wcout << L"Mode: dry-run cut\n"
               << L"Input:  " << config_.inputFile.wstring() << L'\n'
               << L"Output: " << config_.outputFile.wstring() << L'\n'
               << L"Temp:   " << tempDirectory.wstring() << L"\n\n";

    for (size_t index = 0; index < config_.segments.size(); ++index) {
        std::wcout << L"Segment " << (index + 1) << L": "
                   << config_.segments[index].start << L"-" << config_.segments[index].end << L'\n'
                   << runner.BuildFFmpegCommand(BuildCutArguments(config_.segments[index], partFiles[index])) << L"\n\n";
    }

    std::wcout << L"Concat list: " << listFile.wstring() << L'\n';
    for (const auto& part : partFiles) {
        std::wcout << L"  file '" << part.wstring() << L"'\n";
    }
    std::wcout << L'\n'
               << runner.BuildFFmpegCommand(BuildConcatArguments(listFile)) << L"\n\n";
}

void VideoCutter::PrintSummary(bool ok,
                               const std::filesystem::path& tempDirectory,
                               bool tempCreated,
                               bool tempDeleted,
                               const std::wstring& error) const {
    std::error_code ec;
    const std::uintmax_t inputSize = std::filesystem::file_size(config_.inputFile, ec);
    const std::uintmax_t safeInputSize = ec ? 0 : inputSize;
    ec.clear();

    std::uintmax_t outputSize = 0;
    if (!config_.dryRun && std::filesystem::exists(config_.outputFile, ec) && !ec) {
        outputSize = std::filesystem::file_size(config_.outputFile, ec);
        if (ec) {
            outputSize = 0;
        }
    }

    std::wcout << L"Summary\n"
               << L"  Input file:        " << config_.inputFile.wstring() << L'\n'
               << L"  Output file:       " << config_.outputFile.wstring() << L'\n'
               << L"  Segments:          " << config_.segments.size() << L'\n'
               << L"  Total duration:    " << FormatDuration(TotalDurationMilliseconds()) << L'\n'
               << L"  Input size:        " << FormatMegabytes(safeInputSize) << L'\n'
               << L"  Output size:       " << FormatMegabytes(outputSize) << L'\n'
               << L"  Status:            " << (ok ? L"OK" : L"ERROR") << L'\n';

    if (config_.dryRun) {
        std::wcout << L"  Temp cleanup:      not created (dry-run)\n";
    } else if (tempCreated) {
        std::wcout << L"  Temp cleanup:      "
                   << (config_.keepTemp ? L"kept by --keep-temp" : (tempDeleted ? L"deleted" : L"kept"))
                   << L" (" << tempDirectory.wstring() << L")\n";
    } else {
        std::wcout << L"  Temp cleanup:      not created\n";
    }

    if (!error.empty()) {
        std::wcout << L"  Error:             " << error << L'\n';
    }
}

std::int64_t VideoCutter::TotalDurationMilliseconds() const {
    std::int64_t total = 0;
    for (const auto& segment : config_.segments) {
        total += segment.DurationMilliseconds();
    }
    return total;
}

} // namespace video_optimizer
