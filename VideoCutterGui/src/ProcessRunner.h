#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace video_cutter_gui {

struct ProcessResult {
    bool started = false;
    bool canceled = false;
    DWORD exitCode = 1;
    std::wstring output;
    std::wstring error;
};

std::wstring FormatWin32Error(DWORD errorCode);
std::wstring BytesToWide(const std::string& bytes);
std::string WideToUtf8(const std::wstring& text);
std::wstring QuoteArgument(const std::wstring& argument);
std::wstring BuildCommandLine(const std::filesystem::path& executable,
                              const std::vector<std::wstring>& arguments);

class ProcessRunner {
public:
    using ProcessStartedCallback = std::function<void(HANDLE process)>;
    using ProcessFinishedCallback = std::function<void()>;

    ProcessRunner(std::atomic_bool* cancelRequested,
                  ProcessStartedCallback startedCallback = {},
                  ProcessFinishedCallback finishedCallback = {});

    ProcessResult Run(const std::filesystem::path& executable,
                      const std::vector<std::wstring>& arguments) const;

private:
    std::atomic_bool* cancelRequested_ = nullptr;
    ProcessStartedCallback startedCallback_;
    ProcessFinishedCallback finishedCallback_;
};

} // namespace video_cutter_gui
