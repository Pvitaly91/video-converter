#pragma once

#include <filesystem>
#include <string>

namespace video_cutter_gui {

struct OutputPathRequest {
    std::filesystem::path inputFile;
    std::filesystem::path outputFolder;
    std::wstring manualFileName;
};

struct OutputPathResult {
    std::filesystem::path outputFile;
    bool adjusted = false;
    std::wstring note;
};

class OutputPathBuilder {
public:
    static OutputPathResult Build(const OutputPathRequest& request);
};

bool PathsEqual(const std::filesystem::path& left, const std::filesystem::path& right);

} // namespace video_cutter_gui
