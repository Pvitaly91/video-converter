#include "Config.h"

#include "SegmentParser.h"

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

bool IsAnyOption(const std::wstring& value) {
    return value == L"-o" || value.rfind(L"--", 0) == 0 || value == L"-h" || value == L"/?";
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

bool ReadCutOptionValue(int argc, wchar_t* argv[], int& index, std::wstring& value, std::wstring& error) {
    if (index + 1 >= argc) {
        error = std::wstring(L"Missing value for ") + argv[index];
        return false;
    }

    value = argv[++index];
    if (IsAnyOption(value)) {
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

std::filesystem::path ResolveFilePath(const std::filesystem::path& input, std::wstring& error) {
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(input, ec);
    if (ec) {
        error = std::wstring(L"Cannot resolve file path: ") + Widen(ec.message());
        return {};
    }

    if (!std::filesystem::exists(absolute, ec) || ec) {
        error = L"Input file does not exist: " + absolute.wstring();
        return {};
    }

    if (!std::filesystem::is_regular_file(absolute, ec) || ec) {
        error = L"Input path is not a file: " + absolute.wstring();
        return {};
    }

    return absolute;
}

std::filesystem::path ResolveOutputPath(const std::filesystem::path& output) {
    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(output, ec);
    if (ec) {
        absolute = output;
    }

    if (absolute.extension().empty()) {
        absolute += L".mp4";
    }

    return absolute;
}

std::filesystem::path MakeDefaultCutOutputPath(const std::filesystem::path& input) {
    return input.parent_path() / (input.stem().wstring() + L"_cut.mp4");
}

std::filesystem::path MakeSafeCutOutputPath(const std::filesystem::path& preferred) {
    std::error_code ec;
    if (!std::filesystem::exists(preferred, ec) && !ec) {
        return preferred;
    }

    const std::filesystem::path directory = preferred.parent_path();
    const std::wstring stem = preferred.stem().wstring();
    const std::wstring extension = preferred.extension().empty() ? L".mp4" : preferred.extension().wstring();

    for (int index = 2; index < 100000; ++index) {
        const std::filesystem::path candidate = directory / (stem + L"_" + std::to_wstring(index) + extension);
        if (!std::filesystem::exists(candidate, ec) && !ec) {
            return candidate;
        }
    }

    return directory / (stem + L"_safe" + extension);
}

ParseResult ParseCutArguments(int argc, wchar_t* argv[]) {
    ParseResult result;
    result.mode = AppMode::Cut;

    CutConfig config;
    std::filesystem::path inputPath;
    std::filesystem::path outputPath;
    std::wstring segmentsText;
    bool hasInput = false;
    bool hasOutput = false;

    for (int i = 2; i < argc; ++i) {
        const std::wstring arg = argv[i];

        if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
            result.ok = true;
            result.helpRequested = true;
            result.cutHelpRequested = true;
            return result;
        }

        if (arg == L"--segments") {
            if (!ReadCutOptionValue(argc, argv, i, segmentsText, result.error)) {
                return result;
            }
            continue;
        }

        if (arg == L"-o" || arg == L"--output") {
            std::wstring value;
            if (!ReadCutOptionValue(argc, argv, i, value, result.error)) {
                return result;
            }
            outputPath = value;
            hasOutput = true;
            continue;
        }

        if (arg == L"--keep-temp") {
            config.keepTemp = true;
            continue;
        }

        if (arg == L"--dry-run") {
            config.dryRun = true;
            continue;
        }

        if (arg == L"--overwrite") {
            config.overwrite = true;
            continue;
        }

        if (IsAnyOption(arg)) {
            result.error = L"Unknown cut option: " + arg;
            return result;
        }

        if (hasInput) {
            result.error = L"Only one input file can be specified for cut mode.";
            return result;
        }

        inputPath = arg;
        hasInput = true;
    }

    if (!hasInput) {
        result.error = L"Missing input file for cut mode.";
        return result;
    }

    if (segmentsText.empty()) {
        result.error = L"Missing --segments. Example: --segments \"1:00-2:00,5:00-6:00\"";
        return result;
    }

    config.inputFile = ResolveFilePath(inputPath, result.error);
    if (!result.error.empty()) {
        return result;
    }

    if (ToLower(config.inputFile.extension().wstring()) != L".mp4") {
        result.error = L"Cut mode input must be an MP4 file: " + config.inputFile.wstring();
        return result;
    }

    std::wstring segmentError;
    if (!ParseSegmentList(segmentsText, config.segments, segmentError)) {
        result.error = segmentError + L" Example: --segments \"1:00-2:00,5:00-6:00\"";
        return result;
    }

    config.outputFile = hasOutput ? ResolveOutputPath(outputPath) : MakeDefaultCutOutputPath(config.inputFile);

    if (ToLower(config.outputFile.extension().wstring()) != L".mp4") {
        result.error = L"Cut mode output must be an MP4 file: " + config.outputFile.wstring();
        return result;
    }

    std::error_code ec;
    if (std::filesystem::exists(config.outputFile, ec) && !ec && std::filesystem::is_directory(config.outputFile, ec)) {
        result.error = L"Output path is a directory: " + config.outputFile.wstring();
        return result;
    }

    if (!config.overwrite) {
        config.outputFile = MakeSafeCutOutputPath(config.outputFile);
    }

    if (std::filesystem::equivalent(config.inputFile, config.outputFile, ec) && !ec) {
        result.error = L"Output file must be different from input file to keep the original unchanged.";
        return result;
    }

    result.cutConfig = config;
    result.ok = true;
    return result;
}

} // namespace

ParseResult ParseArguments(int argc, wchar_t* argv[]) {
    if (argc > 1 && std::wstring(argv[1]) == L"cut") {
        return ParseCutArguments(argc, argv);
    }

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
        << L"VideoOptimizer - FFmpeg-based MP4 optimizer and fast segment cutter\n\n"
        << L"Usage:\n"
        << L"  VideoOptimizer.exe [folder] [options]\n"
        << L"  VideoOptimizer.exe cut <input.mp4> --segments \"START-END,START-END\" [-o output.mp4] [options]\n\n"
        << L"Optimize examples:\n"
        << L"  VideoOptimizer.exe \"D:\\Videos\" --recursive\n"
        << L"  VideoOptimizer.exe \"D:\\Videos\" --crf 22 --preset slow --fps 30 --max-height 1080\n"
        << L"  VideoOptimizer.exe \"D:\\Videos\" --dry-run\n\n"
        << L"Cut examples:\n"
        << L"  VideoOptimizer.exe cut \"D:\\Videos\\big.mp4\" --segments \"1:00-2:00,5:00-6:00,10:00-15:00\" -o \"D:\\Videos\\result.mp4\"\n"
        << L"  VideoOptimizer.exe cut \"D:\\Videos\\big.mp4\" --segments \"00:01:00-00:02:00\" --dry-run\n\n"
        << L"Optimize options:\n"
        << L"  --crf N                H.264 quality, default 22, valid range 0-51\n"
        << L"  --preset NAME          x264 preset, default slow\n"
        << L"  --fps N                Output FPS, default 30\n"
        << L"  --max-height N         Downscale only when video is higher than N, default 1080\n"
        << L"  --audio-bitrate RATE   AAC bitrate, default 160k\n"
        << L"  --recursive            Scan subdirectories\n"
        << L"  --overwrite            Overwrite matching output files\n"
        << L"  --dry-run              Show planned work and FFmpeg commands without converting\n"
        << L"  --help                 Show this help\n\n";

    PrintCutHelp();
}

void PrintCutHelp() {
    std::wcout
        << L"Cut mode: fast MP4 segment cut and concat without re-encoding\n\n"
        << L"Usage:\n"
        << L"  VideoOptimizer.exe cut <input.mp4> --segments \"START-END,START-END\" [-o output.mp4] [options]\n\n"
        << L"Cut mode options:\n"
        << L"  --segments LIST        Segment list: START-END,START-END\n"
        << L"  -o, --output FILE      Output MP4 path. Default: input_cut.mp4\n"
        << L"  --keep-temp            Keep temporary part files after success\n"
        << L"  --overwrite            Allow overwriting the selected output file\n"
        << L"  --dry-run              Show FFmpeg commands without cutting\n"
        << L"  --help                 Show cut help\n\n"
        << L"Supported time formats:\n"
        << L"  SS\n"
        << L"  MM:SS\n"
        << L"  HH:MM:SS\n"
        << L"  HH:MM:SS.mmm\n\n"
        << L"Cut examples:\n"
        << L"  VideoOptimizer.exe cut \"D:\\Videos\\big.mp4\" --segments \"60-120\" -o \"D:\\Videos\\result.mp4\"\n"
        << L"  VideoOptimizer.exe cut \"D:\\Videos\\big.mp4\" --segments \"1:00-2:00,5:00-6:00\" --dry-run\n";
}

} // namespace video_optimizer
