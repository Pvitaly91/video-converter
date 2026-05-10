#include "VideoOptimizer.h"

#include "FileScanner.h"
#include "VideoProbe.h"

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

constexpr wchar_t kOutputDirectoryName[] = L"_optimized_mp4";

std::wstring FormatMegabytes(std::uintmax_t bytes) {
    std::wostringstream stream;
    stream << std::fixed << std::setprecision(2) << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << L" MB";
    return stream.str();
}

std::wstring FormatSignedMegabytes(long double bytes) {
    std::wostringstream stream;
    stream << std::fixed << std::setprecision(2) << (bytes / (1024.0L * 1024.0L)) << L" MB";
    return stream.str();
}

std::wstring FormatPercent(long double value) {
    std::wostringstream stream;
    stream << std::fixed << std::setprecision(2) << value << L"%";
    return stream.str();
}

std::wstring Widen(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}

std::wstring StatusToString(FileStatus status) {
    switch (status) {
    case FileStatus::Ok:
        return L"OK";
    case FileStatus::Skipped:
        return L"SKIPPED";
    case FileStatus::Error:
        return L"ERROR";
    case FileStatus::BiggerThanOriginal:
        return L"BIGGER_THAN_ORIGINAL";
    }

    return L"UNKNOWN";
}

} // namespace

VideoOptimizer::VideoOptimizer(Config config)
    : config_(std::move(config)) {
}

int VideoOptimizer::Run() {
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

    std::wcout << L"Target directory: " << config_.targetDirectory.wstring() << L'\n'
               << L"Output directory: " << (config_.targetDirectory / kOutputDirectoryName).wstring() << L'\n'
               << L"Mode: " << (config_.dryRun ? L"dry-run" : L"convert") << L'\n'
               << L"Recursive: " << (config_.recursive ? L"yes" : L"no") << L"\n\n";

    const FileScanner scanner(config_.targetDirectory, config_.recursive);
    const ScanResult scan = scanner.Scan();

    for (const auto& warning : scan.warnings) {
        std::wcerr << L"Warning: " << warning << L'\n';
    }

    Summary summary;
    summary.found = scan.files.size();

    if (scan.files.empty()) {
        PrintSummary(summary);
        return 0;
    }

    for (const auto& file : scan.files) {
        FileResult result = ProcessFile(file, runner);
        PrintFileResult(result);
        AddToSummary(result, summary);
    }

    PrintSummary(summary);
    return summary.errors == 0 ? 0 : 2;
}

FileResult VideoOptimizer::ProcessFile(const std::filesystem::path& input, const FFmpegRunner& runner) const {
    FileResult result;
    result.input = input;
    result.output = BuildOutputPath(input);

    std::error_code ec;
    result.sizeBefore = std::filesystem::file_size(input, ec);
    if (ec) {
        result.status = FileStatus::Error;
        result.message = std::wstring(L"Cannot read input file size: ") + Widen(ec.message());
        return result;
    }

    if (result.sizeBefore == 0) {
        result.status = FileStatus::Skipped;
        result.message = L"Input file has zero size.";
        return result;
    }

    result.output = MakeUniqueOutputPath(result.output);

    if (config_.dryRun) {
        result.status = FileStatus::Skipped;
        result.message = L"Dry-run only. Command: " + runner.BuildFFmpegCommand(input, result.output, config_);
        return result;
    }

    VideoProbe probe(runner);
    const ProbeResult probeResult = probe.Probe(input);
    if (!probeResult.ok) {
        result.status = FileStatus::Error;
        result.message = probeResult.error;
        return result;
    }

    std::filesystem::create_directories(result.output.parent_path(), ec);
    if (ec) {
        result.status = FileStatus::Error;
        result.message = std::wstring(L"Cannot create output directory: ") + Widen(ec.message());
        return result;
    }

    const ProcessResult process = runner.RunFFmpeg(input, result.output, config_);
    if (!process.started) {
        result.status = FileStatus::Error;
        result.message = process.error;
        return result;
    }

    if (process.exitCode != 0) {
        result.status = FileStatus::Error;
        result.message = L"ffmpeg failed with exit code " + std::to_wstring(process.exitCode);
        if (!process.output.empty()) {
            result.message += L"\n" + process.output;
        }
        return result;
    }

    result.sizeAfter = std::filesystem::file_size(result.output, ec);
    if (ec) {
        result.status = FileStatus::Error;
        result.message = std::wstring(L"Cannot read output file size: ") + Widen(ec.message());
        return result;
    }

    result.status = result.sizeAfter > result.sizeBefore ? FileStatus::BiggerThanOriginal : FileStatus::Ok;
    if (result.status == FileStatus::BiggerThanOriginal) {
        result.message = L"Output is larger than original. File was kept for manual review.";
    }

    return result;
}

std::filesystem::path VideoOptimizer::BuildOutputPath(const std::filesystem::path& input) const {
    std::filesystem::path outputDirectory = config_.targetDirectory / kOutputDirectoryName;

    if (config_.recursive) {
        std::error_code ec;
        const std::filesystem::path relativeParent = std::filesystem::relative(input.parent_path(), config_.targetDirectory, ec);
        if (!ec && !relativeParent.empty() && relativeParent != L".") {
            outputDirectory /= relativeParent;
        }
    }

    return outputDirectory / (input.stem().wstring() + L".mp4");
}

std::filesystem::path VideoOptimizer::MakeUniqueOutputPath(const std::filesystem::path& preferred) const {
    if (config_.overwrite) {
        return preferred;
    }

    std::error_code ec;
    if (!std::filesystem::exists(preferred, ec) && !ec) {
        return preferred;
    }

    const std::filesystem::path directory = preferred.parent_path();
    const std::wstring stem = preferred.stem().wstring();
    const std::wstring extension = preferred.extension().wstring();

    std::filesystem::path candidate = directory / (stem + L"_optimized" + extension);
    if (!std::filesystem::exists(candidate, ec) && !ec) {
        return candidate;
    }

    for (int index = 2; index < 100000; ++index) {
        candidate = directory / (stem + L"_optimized_" + std::to_wstring(index) + extension);
        if (!std::filesystem::exists(candidate, ec) && !ec) {
            return candidate;
        }
    }

    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return directory / (stem + L"_optimized_" + std::to_wstring(ticks) + extension);
}

void VideoOptimizer::PrintFileResult(const FileResult& result) const {
    std::wcout << L"File: " << result.input.filename().wstring() << L'\n'
               << L"  Input:  " << result.input.wstring() << L'\n'
               << L"  Output: " << result.output.wstring() << L'\n'
               << L"  Before: " << FormatMegabytes(result.sizeBefore) << L'\n';

    if (result.status == FileStatus::Ok || result.status == FileStatus::BiggerThanOriginal) {
        const long double savedBytes = static_cast<long double>(result.sizeBefore) - static_cast<long double>(result.sizeAfter);
        const long double savedPercent = result.sizeBefore == 0
            ? 0.0L
            : (savedBytes * 100.0L / static_cast<long double>(result.sizeBefore));

        std::wcout << L"  After:  " << FormatMegabytes(result.sizeAfter) << L'\n'
                   << L"  Saved:  " << FormatSignedMegabytes(savedBytes) << L" (" << FormatPercent(savedPercent) << L")\n";
    }

    std::wcout << L"  Status: " << StatusToString(result.status) << L'\n';
    if (!result.message.empty()) {
        std::wcout << L"  Message: " << result.message << L'\n';
    }
    std::wcout << L'\n';
}

void VideoOptimizer::AddToSummary(const FileResult& result, Summary& summary) const {
    switch (result.status) {
    case FileStatus::Ok:
        ++summary.processed;
        summary.totalBefore += result.sizeBefore;
        summary.totalAfter += result.sizeAfter;
        break;
    case FileStatus::BiggerThanOriginal:
        ++summary.processed;
        ++summary.biggerThanOriginal;
        summary.totalBefore += result.sizeBefore;
        summary.totalAfter += result.sizeAfter;
        break;
    case FileStatus::Skipped:
        ++summary.skipped;
        break;
    case FileStatus::Error:
        ++summary.errors;
        break;
    }
}

void VideoOptimizer::PrintSummary(const Summary& summary) const {
    const long double savedBytes = static_cast<long double>(summary.totalBefore) - static_cast<long double>(summary.totalAfter);
    const long double savedPercent = summary.totalBefore == 0
        ? 0.0L
        : (savedBytes * 100.0L / static_cast<long double>(summary.totalBefore));

    std::wcout << L"Summary\n"
               << L"  Found video files:       " << summary.found << L'\n'
               << L"  Successfully processed: " << summary.processed << L'\n'
               << L"  Skipped:                 " << summary.skipped << L'\n'
               << L"  Errors:                  " << summary.errors << L'\n'
               << L"  Bigger than original:    " << summary.biggerThanOriginal << L'\n'
               << L"  Total before:            " << FormatMegabytes(summary.totalBefore) << L'\n'
               << L"  Total after:             " << FormatMegabytes(summary.totalAfter) << L'\n'
               << L"  Total saved:             " << FormatSignedMegabytes(savedBytes) << L" (" << FormatPercent(savedPercent) << L")\n";
}

} // namespace video_optimizer
