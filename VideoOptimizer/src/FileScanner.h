#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace video_optimizer {

struct ScanResult {
    std::vector<std::filesystem::path> files;
    std::vector<std::wstring> warnings;
};

class FileScanner {
public:
    FileScanner(std::filesystem::path root, bool recursive);

    ScanResult Scan() const;

private:
    bool ShouldSkipDirectory(const std::filesystem::path& directory) const;
    bool IsSupportedVideoFile(const std::filesystem::path& file) const;

    std::filesystem::path root_;
    bool recursive_ = false;
};

} // namespace video_optimizer
