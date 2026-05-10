#pragma once

#include <filesystem>
#include <string>

namespace video_optimizer {

struct Config {
    std::filesystem::path targetDirectory;
    bool recursive = false;
    bool overwrite = false;
    bool dryRun = false;
    int crf = 22;
    std::wstring preset = L"slow";
    int fps = 30;
    int maxHeight = 1080;
    std::wstring audioBitrate = L"160k";
};

struct ParseResult {
    bool ok = false;
    bool helpRequested = false;
    Config config;
    std::wstring error;
};

ParseResult ParseArguments(int argc, wchar_t* argv[]);
void PrintHelp();

} // namespace video_optimizer
