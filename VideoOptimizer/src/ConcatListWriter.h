#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace video_optimizer {

class ConcatListWriter {
public:
    static bool Write(const std::filesystem::path& listFile,
                      const std::vector<std::filesystem::path>& partFiles,
                      std::wstring& error);
};

} // namespace video_optimizer
