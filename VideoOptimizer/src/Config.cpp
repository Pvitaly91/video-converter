#include "Config.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace video_optimizer {
namespace {

bool IsOption(const std::wstring& value) {
    return value.rfind(L"--", 0) == 0;
}

std::wstring Widen(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}

std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool ParseInt(const std::wstring& value, int& result) {
    try {
        size_t parsed = 0;
        const int number = std::stoi(value, &parsed, 10);
        if (parsed != value.size()) {
            return false;
        }
        result = number;
        return true;
    } catch (...) {
        return false;
    }
}

bool IsValidPreset(const std::wstring& preset) {
    static const std::vector<std::wstring> presets = {
        L"ultrafast", L"superfast", L"veryfast", L"faster", L"fast",
        L"medium", L"slow", L"slower", L"veryslow", L"placebo"
    };
    const std::wstring normalized = ToLower(preset);
    return std::find(presets.begin(), presets.end(), normalized) != presets.end();
}

bool IsValidAudioBitrate(const std::wstring& bitrate) {
    if (bitrate.size() < 2) {
        return false;
    }

    const wchar_t suffix = static_cast<wchar_t>(std::towlower(bitrate.back()));
    if (suffix != L'k') {
        return false;
    }

    return std::all_of(bitrate.begin(), bitrate.end() - 1, [](wchar_t ch) {
        return std::iswdigit(ch) != 0;
    });
}

bool ReadOptionValue(int argc, wchar_t* argv[], int& index, std::wstring& value, std::wstring& error) {
    if (index + 1 >= argc) {
        error = std::wstring(L"Missing value for ") + argv[index];
        return false;
    }

    value = argv[++index];
    if (IsOption(value)) {
        error = std::wstring(L"Missing value for ") + argv[index - 1];
        return false;
    }

    return true;
}

std::filesystem::path ResolveTargetDirectory(const std::filesystem::path& input, std::wstring& error) {
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(input, ec);
    if (ec) {
        error = std::wstring(L"Cannot resolve target directory: ") + Widen(ec.message());
        return {};
    }

    if (!std::filesystem::exists(absolute, ec) || ec) {
        error = L"Target directory does not exist: " + absolute.wstring();
        return {};
    }

    if (!std::filesystem::is_directory(absolute, ec) || ec) {
        error = L"Target path is not a directory: " + absolute.wstring();
        return {};
    }

    return absolute;
}

} // namespace

ParseResult ParseArguments(int argc, wchar_t* argv[]) {
    ParseResult result;
    Config config;
    std::filesystem::path requestedPath;
    bool hasPath = false;

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];

        if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
            result.ok = true;
            result.helpRequested = true;
            return result;
        }

        if (arg == L"--recursive") {
            config.recursive = true;
            continue;
        }

        if (arg == L"--overwrite") {
            config.overwrite = true;
            continue;
        }

        if (arg == L"--dry-run") {
            config.dryRun = true;
            continue;
        }

        if (arg == L"--crf") {
            std::wstring value;
            if (!ReadOptionValue(argc, argv, i, value, result.error)) {
                return result;
            }
            if (!ParseInt(value, config.crf) || config.crf < 0 || config.crf > 51) {
                result.error = L"--crf must be an integer from 0 to 51.";
                return result;
            }
            continue;
        }

        if (arg == L"--preset") {
            std::wstring value;
            if (!ReadOptionValue(argc, argv, i, value, result.error)) {
                return result;
            }
            config.preset = ToLower(value);
            if (!IsValidPreset(config.preset)) {
                result.error = L"--preset must be one of: ultrafast, superfast, veryfast, faster, fast, medium, slow, slower, veryslow, placebo.";
                return result;
            }
            continue;
        }

        if (arg == L"--fps") {
            std::wstring value;
            if (!ReadOptionValue(argc, argv, i, value, result.error)) {
                return result;
            }
            if (!ParseInt(value, config.fps) || config.fps < 1 || config.fps > 240) {
                result.error = L"--fps must be an integer from 1 to 240.";
                return result;
            }
            continue;
        }

        if (arg == L"--max-height") {
            std::wstring value;
            if (!ReadOptionValue(argc, argv, i, value, result.error)) {
                return result;
            }
            if (!ParseInt(value, config.maxHeight) || config.maxHeight < 2 || config.maxHeight > 4320) {
                result.error = L"--max-height must be an integer from 2 to 4320.";
                return result;
            }
            continue;
        }

        if (arg == L"--audio-bitrate") {
            std::wstring value;
            if (!ReadOptionValue(argc, argv, i, value, result.error)) {
                return result;
            }
            config.audioBitrate = ToLower(value);
            if (!IsValidAudioBitrate(config.audioBitrate)) {
                result.error = L"--audio-bitrate must look like 128k, 160k, or 192k.";
                return result;
            }
            continue;
        }

        if (IsOption(arg)) {
            result.error = L"Unknown option: " + arg;
            return result;
        }

        if (hasPath) {
            result.error = L"Only one target directory can be specified.";
            return result;
        }

        requestedPath = arg;
        hasPath = true;
    }

    if (!hasPath) {
        std::error_code ec;
        requestedPath = std::filesystem::current_path(ec);
        if (ec) {
            result.error = std::wstring(L"Cannot read current directory: ") + Widen(ec.message());
            return result;
        }
    }

    config.targetDirectory = ResolveTargetDirectory(requestedPath, result.error);
    if (!result.error.empty()) {
        return result;
    }

    result.config = config;
    result.ok = true;
    return result;
}

void PrintHelp() {
    std::wcout
        << L"VideoOptimizer - batch video optimizer for MP4/H.264 using FFmpeg\n\n"
        << L"Usage:\n"
        << L"  VideoOptimizer.exe\n"
        << L"  VideoOptimizer.exe \"D:\\Videos\"\n"
        << L"  VideoOptimizer.exe \"D:\\Videos\" --recursive\n"
        << L"  VideoOptimizer.exe \"D:\\Videos\" --crf 22 --preset slow --fps 30 --max-height 1080\n"
        << L"  VideoOptimizer.exe \"D:\\Videos\" --overwrite\n"
        << L"  VideoOptimizer.exe \"D:\\Videos\" --dry-run\n\n"
        << L"Options:\n"
        << L"  --crf N                H.264 quality, default 22, valid range 0-51\n"
        << L"  --preset NAME          x264 preset, default slow\n"
        << L"  --fps N                Output FPS, default 30\n"
        << L"  --max-height N         Downscale only when video is higher than N, default 1080\n"
        << L"  --audio-bitrate RATE   AAC bitrate, default 160k\n"
        << L"  --recursive            Scan subdirectories\n"
        << L"  --overwrite            Overwrite matching output files\n"
        << L"  --dry-run              Show planned work and FFmpeg commands without converting\n"
        << L"  --help                 Show this help\n";
}

} // namespace video_optimizer
