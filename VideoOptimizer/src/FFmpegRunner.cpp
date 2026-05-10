#include "FFmpegRunner.h"

#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace video_optimizer {
namespace {

std::wstring FormatWin32Error(unsigned long errorCode) {
    wchar_t* messageBuffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&messageBuffer),
        0,
        nullptr);

    if (length == 0 || messageBuffer == nullptr) {
        return L"Windows error " + std::to_wstring(errorCode);
    }

    std::wstring message(messageBuffer, length);
    LocalFree(messageBuffer);

    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L'.')) {
        message.pop_back();
    }

    return message;
}

std::wstring BytesToWide(const std::string& bytes) {
    if (bytes.empty()) {
        return {};
    }

    auto convert = [&bytes](UINT codePage, DWORD flags) -> std::wstring {
        int size = MultiByteToWideChar(codePage, flags, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        if (size <= 0) {
            return {};
        }

        std::wstring wide(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(codePage, flags, bytes.data(), static_cast<int>(bytes.size()), wide.data(), size);
        return wide;
    };

    std::wstring result = convert(CP_UTF8, MB_ERR_INVALID_CHARS);
    if (!result.empty()) {
        return result;
    }

    result = convert(CP_ACP, 0);
    return result.empty() ? L"<process output could not be decoded>" : result;
}

std::wstring QuoteArgument(const std::wstring& argument) {
    if (argument.empty()) {
        return L"\"\"";
    }

    const bool needsQuotes = argument.find_first_of(L" \t\n\v\"") != std::wstring::npos;
    if (!needsQuotes) {
        return argument;
    }

    std::wstring quoted = L"\"";
    size_t backslashes = 0;

    for (wchar_t ch : argument) {
        if (ch == L'\\') {
            ++backslashes;
        } else if (ch == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(ch);
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            backslashes = 0;
            quoted.push_back(ch);
        }
    }

    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::wstring BuildCommandLine(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments) {
    std::wostringstream command;
    command << QuoteArgument(executable.wstring());
    for (const auto& argument : arguments) {
        command << L' ' << QuoteArgument(argument);
    }
    return command.str();
}

bool IsRegularFile(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && !ec;
}

std::filesystem::path ExeDirectory() {
    std::wstring buffer(MAX_PATH, L'\0');

    while (true) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return std::filesystem::current_path();
        }

        if (length < buffer.size() - 1) {
            buffer.resize(length);
            return std::filesystem::path(buffer).parent_path();
        }

        buffer.resize(buffer.size() * 2);
    }
}

std::filesystem::path SearchInPath(const std::wstring& name) {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = SearchPathW(nullptr, name.c_str(), nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (length == 0) {
        return {};
    }

    if (length >= buffer.size()) {
        buffer.assign(static_cast<size_t>(length) + 1, L'\0');
        length = SearchPathW(nullptr, name.c_str(), nullptr, static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (length == 0 || length >= buffer.size()) {
            return {};
        }
    }

    buffer.resize(length);
    return std::filesystem::path(buffer);
}

std::vector<std::wstring> BuildFFmpegArguments(const std::filesystem::path& input,
                                               const std::filesystem::path& output,
                                               const Config& config) {
    const std::wstring scaleFilter =
        L"scale=-2:trunc(min(" + std::to_wstring(config.maxHeight) + L"\\,ih)/2)*2,fps=" + std::to_wstring(config.fps);

    std::vector<std::wstring> arguments;
    arguments.push_back(config.overwrite ? L"-y" : L"-n");
    arguments.push_back(L"-i");
    arguments.push_back(input.wstring());
    arguments.push_back(L"-map");
    arguments.push_back(L"0:v:0");
    arguments.push_back(L"-map");
    arguments.push_back(L"0:a?");
    arguments.push_back(L"-c:v");
    arguments.push_back(L"libx264");
    arguments.push_back(L"-preset");
    arguments.push_back(config.preset);
    arguments.push_back(L"-crf");
    arguments.push_back(std::to_wstring(config.crf));
    arguments.push_back(L"-pix_fmt");
    arguments.push_back(L"yuv420p");
    arguments.push_back(L"-vf");
    arguments.push_back(scaleFilter);
    arguments.push_back(L"-c:a");
    arguments.push_back(L"aac");
    arguments.push_back(L"-b:a");
    arguments.push_back(config.audioBitrate);
    arguments.push_back(L"-ar");
    arguments.push_back(L"48000");
    arguments.push_back(L"-ac");
    arguments.push_back(L"2");
    arguments.push_back(L"-movflags");
    arguments.push_back(L"+faststart");
    arguments.push_back(output.wstring());
    return arguments;
}

} // namespace

bool FFmpegRunner::Locate() {
    const std::filesystem::path pathFfmpeg = SearchInPath(L"ffmpeg.exe");
    const std::filesystem::path pathFfprobe = SearchInPath(L"ffprobe.exe");

    if (IsRegularFile(pathFfmpeg) && IsRegularFile(pathFfprobe)) {
        ffmpegPath_ = pathFfmpeg;
        ffprobePath_ = pathFfprobe;
        return true;
    }

    const std::filesystem::path localBin = ExeDirectory() / L"tools" / L"ffmpeg" / L"bin";
    const std::filesystem::path localFfmpeg = localBin / L"ffmpeg.exe";
    const std::filesystem::path localFfprobe = localBin / L"ffprobe.exe";

    if (IsRegularFile(localFfmpeg) && IsRegularFile(localFfprobe)) {
        ffmpegPath_ = localFfmpeg;
        ffprobePath_ = localFfprobe;
        return true;
    }

    if (IsRegularFile(pathFfmpeg)) {
        ffmpegPath_ = pathFfmpeg;
    } else if (IsRegularFile(localFfmpeg)) {
        ffmpegPath_ = localFfmpeg;
    }

    if (IsRegularFile(pathFfprobe)) {
        ffprobePath_ = pathFfprobe;
    } else if (IsRegularFile(localFfprobe)) {
        ffprobePath_ = localFfprobe;
    }

    return false;
}

bool FFmpegRunner::IsAvailable() const {
    return !ffmpegPath_.empty() && !ffprobePath_.empty();
}

const std::filesystem::path& FFmpegRunner::FfmpegPath() const {
    return ffmpegPath_;
}

const std::filesystem::path& FFmpegRunner::FfprobePath() const {
    return ffprobePath_;
}

std::wstring FFmpegRunner::MissingToolsMessage() const {
    return L"FFmpeg not found. Install FFmpeg or place ffmpeg.exe and ffprobe.exe into tools\\ffmpeg\\bin.";
}

std::wstring FFmpegRunner::BuildFFmpegCommand(const std::filesystem::path& input,
                                              const std::filesystem::path& output,
                                              const Config& config) const {
    const std::filesystem::path executable = ffmpegPath_.empty() ? std::filesystem::path(L"ffmpeg.exe") : ffmpegPath_;
    return BuildCommandLine(executable, BuildFFmpegArguments(input, output, config));
}

std::wstring FFmpegRunner::BuildFFmpegCommand(const std::vector<std::wstring>& arguments) const {
    const std::filesystem::path executable = ffmpegPath_.empty() ? std::filesystem::path(L"ffmpeg.exe") : ffmpegPath_;
    return BuildCommandLine(executable, arguments);
}

std::wstring FFmpegRunner::BuildFFprobeCommand(const std::vector<std::wstring>& arguments) const {
    const std::filesystem::path executable = ffprobePath_.empty() ? std::filesystem::path(L"ffprobe.exe") : ffprobePath_;
    return BuildCommandLine(executable, arguments);
}

ProcessResult FFmpegRunner::RunFFprobe(const std::filesystem::path& input) const {
    std::vector<std::wstring> arguments;
    arguments.push_back(L"-v");
    arguments.push_back(L"error");
    arguments.push_back(L"-select_streams");
    arguments.push_back(L"v:0");
    arguments.push_back(L"-show_entries");
    arguments.push_back(L"stream=codec_name,width,height");
    arguments.push_back(L"-of");
    arguments.push_back(L"default=noprint_wrappers=1");
    arguments.push_back(input.wstring());

    return RunProcess(ffprobePath_, arguments);
}

ProcessResult FFmpegRunner::RunFFprobe(const std::vector<std::wstring>& arguments) const {
    return RunProcess(ffprobePath_, arguments);
}

ProcessResult FFmpegRunner::RunFFmpeg(const std::filesystem::path& input,
                                      const std::filesystem::path& output,
                                      const Config& config) const {
    return RunProcess(ffmpegPath_, BuildFFmpegArguments(input, output, config));
}

ProcessResult FFmpegRunner::RunFFmpeg(const std::vector<std::wstring>& arguments) const {
    return RunProcess(ffmpegPath_, arguments);
}

ProcessResult FFmpegRunner::RunProcess(const std::filesystem::path& executable,
                                       const std::vector<std::wstring>& arguments) const {
    ProcessResult result;

    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0)) {
        result.error = L"CreatePipe failed: " + FormatWin32Error(GetLastError());
        return result;
    }

    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE nullInput = CreateFileW(
        L"NUL",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &securityAttributes,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = writePipe;
    startupInfo.hStdError = writePipe;
    startupInfo.hStdInput = nullInput == INVALID_HANDLE_VALUE ? nullptr : nullInput;

    PROCESS_INFORMATION processInfo{};
    std::wstring commandLine = BuildCommandLine(executable, arguments);

    const BOOL created = CreateProcessW(
        executable.wstring().c_str(),
        commandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);

    CloseHandle(writePipe);
    if (nullInput != INVALID_HANDLE_VALUE) {
        CloseHandle(nullInput);
    }

    if (!created) {
        CloseHandle(readPipe);
        result.error = L"CreateProcessW failed: " + FormatWin32Error(GetLastError());
        return result;
    }

    result.started = true;
    std::string outputBytes;
    char buffer[4096];
    DWORD bytesRead = 0;

    while (ReadFile(readPipe, buffer, static_cast<DWORD>(sizeof(buffer)), &bytesRead, nullptr) && bytesRead > 0) {
        outputBytes.append(buffer, buffer + bytesRead);
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 1;
    if (GetExitCodeProcess(processInfo.hProcess, &exitCode)) {
        result.exitCode = exitCode;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    CloseHandle(readPipe);

    result.output = BytesToWide(outputBytes);
    return result;
}

} // namespace video_optimizer
