#pragma once

#include "Segment.h"

#include <filesystem>
#include <string>
#include <vector>

namespace video_optimizer {

enum class AppMode {
    Optimize,
    Cut
};

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

struct CutConfig {
    std::filesystem::path inputFile;
    std::filesystem::path outputFile;
    bool keepTemp = false;
    bool overwrite = false;
    bool dryRun = false;
    std::vector<Segment> segments;
};

struct ParseResult {
    bool ok = false;
    bool helpRequested = false;
    bool cutHelpRequested = false;
    AppMode mode = AppMode::Optimize;
    Config config;
    CutConfig cutConfig;
    std::wstring error;
};

ParseResult ParseArguments(int argc, wchar_t* argv[]);
void PrintHelp();
void PrintCutHelp();

} // namespace video_optimizer
