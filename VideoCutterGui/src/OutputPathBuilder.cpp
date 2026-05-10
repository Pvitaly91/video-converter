#include "OutputPathBuilder.h"

#include "TimeParser.h"

#include <Windows.h>

#include <chrono>
#include <ctime>
#include <cwchar>
#include <filesystem>
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

std::filesystem::path NormalizeFolder(const std::filesystem::path& inputFile,
                                      const std::filesystem::path& outputFolder) {
    if (!outputFolder.empty()) {
        return outputFolder;
    }

    return inputFile.parent_path();
}

std::filesystem::path EnsureMp4Extension(std::filesystem::path fileName, bool& adjusted) {
    if (fileName.extension().empty()) {
        fileName += L".mp4";
        adjusted = true;
        return fileName;
    }

    if (_wcsicmp(fileName.extension().wstring().c_str(), L".mp4") != 0) {
        fileName.replace_extension(L".mp4");
        adjusted = true;
    }

    return fileName;
}

std::filesystem::path MakeUnique(const std::filesystem::path& preferred, bool& adjusted) {
    std::error_code ec;
    if (!std::filesystem::exists(preferred, ec) && !ec) {
        return preferred;
    }

    adjusted = true;
    const std::filesystem::path directory = preferred.parent_path();
    const std::wstring stem = preferred.stem().wstring();
    const std::wstring extension = preferred.extension().empty() ? L".mp4" : preferred.extension().wstring();

    for (int index = 2; index < 100000; ++index) {
        const std::filesystem::path candidate = directory / (stem + L"_" + std::to_wstring(index) + extension);
        if (!std::filesystem::exists(candidate, ec) && !ec) {
            return candidate;
        }
    }

    return directory / (stem + L"_" + TimestampId() + extension);
}

std::wstring BuildNote(bool manualName, bool sameFolder, bool adjusted) {
    if (manualName && adjusted) {
        return L"Manual file name was normalized to .mp4 or made unique to avoid overwrite.";
    }

    if (sameFolder) {
        return L"Output is in the input folder, so a unique _cut timestamp name is used.";
    }

    if (adjusted) {
        return L"Output path was made unique to avoid overwrite.";
    }

    return {};
}

} // namespace

bool PathsEqual(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::error_code ec;
    if (std::filesystem::equivalent(left, right, ec) && !ec) {
        return true;
    }

    const std::wstring normalizedLeft = left.lexically_normal().wstring();
    const std::wstring normalizedRight = right.lexically_normal().wstring();
    return _wcsicmp(normalizedLeft.c_str(), normalizedRight.c_str()) == 0;
}

OutputPathResult OutputPathBuilder::Build(const OutputPathRequest& request) {
    OutputPathResult result;
    if (request.inputFile.empty()) {
        return result;
    }

    bool adjusted = false;
    const std::filesystem::path folder = NormalizeFolder(request.inputFile, request.outputFolder);
    const bool sameFolder = PathsEqual(folder, request.inputFile.parent_path());
    const std::wstring manual = Trim(request.manualFileName);

    if (!manual.empty()) {
        std::filesystem::path fileName = std::filesystem::path(manual).filename();
        fileName = EnsureMp4Extension(fileName, adjusted);
        result.outputFile = folder / fileName;
    } else if (sameFolder) {
        result.outputFile = folder / (request.inputFile.stem().wstring() + L"_cut_" + TimestampId() + L".mp4");
        adjusted = true;
    } else {
        std::filesystem::path fileName = request.inputFile.filename();
        fileName = EnsureMp4Extension(fileName, adjusted);
        result.outputFile = folder / fileName;
    }

    if (PathsEqual(result.outputFile, request.inputFile)) {
        result.outputFile = result.outputFile.parent_path()
            / (result.outputFile.stem().wstring() + L"_cut_" + TimestampId() + L".mp4");
        adjusted = true;
    }

    result.outputFile = MakeUnique(result.outputFile, adjusted);
    result.adjusted = adjusted;
    result.note = BuildNote(!manual.empty(), sameFolder, adjusted);
    return result;
}

} // namespace video_cutter_gui
