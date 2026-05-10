#include "ProcessRunner.h"

#include <algorithm>

namespace video_cutter_gui {

std::wstring FormatWin32Error(DWORD errorCode) {
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
        const int size = MultiByteToWideChar(
            codePage,
            flags,
            bytes.data(),
            static_cast<int>(bytes.size()),
            nullptr,
            0);

        if (size <= 0) {
            return {};
        }

        std::wstring wide(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(
            codePage,
            flags,
            bytes.data(),
            static_cast<int>(bytes.size()),
            wide.data(),
            size);
        return wide;
    };

    std::wstring result = convert(CP_UTF8, MB_ERR_INVALID_CHARS);
    if (!result.empty()) {
        return result;
    }

    result = convert(CP_ACP, 0);
    return result.empty() ? L"<process output could not be decoded>" : result;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    if (size <= 0) {
        return {};
    }

    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size,
        nullptr,
        nullptr);
    return result;
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

std::wstring BuildCommandLine(const std::filesystem::path& executable,
                              const std::vector<std::wstring>& arguments) {
    std::wstring command = QuoteArgument(executable.wstring());
    for (const std::wstring& argument : arguments) {
        command += L" ";
        command += QuoteArgument(argument);
    }
    return command;
}

ProcessRunner::ProcessRunner(std::atomic_bool* cancelRequested,
                             ProcessStartedCallback startedCallback,
                             ProcessFinishedCallback finishedCallback)
    : cancelRequested_(cancelRequested),
      startedCallback_(std::move(startedCallback)),
      finishedCallback_(std::move(finishedCallback)) {
}

ProcessResult ProcessRunner::Run(const std::filesystem::path& executable,
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
    if (startedCallback_) {
        startedCallback_(processInfo.hProcess);
    }

    std::string outputBytes;
    char buffer[4096];
    bool processExited = false;

    while (!processExited) {
        if (cancelRequested_ != nullptr && cancelRequested_->load()) {
            TerminateProcess(processInfo.hProcess, 1);
            result.canceled = true;
        }

        DWORD available = 0;
        if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
            DWORD bytesRead = 0;
            const DWORD toRead = std::min<DWORD>(available, static_cast<DWORD>(sizeof(buffer)));
            if (ReadFile(readPipe, buffer, toRead, &bytesRead, nullptr) && bytesRead > 0) {
                outputBytes.append(buffer, buffer + bytesRead);
            }
            continue;
        }

        const DWORD wait = WaitForSingleObject(processInfo.hProcess, 50);
        processExited = (wait == WAIT_OBJECT_0);
    }

    while (true) {
        DWORD available = 0;
        if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) || available == 0) {
            break;
        }

        DWORD bytesRead = 0;
        const DWORD toRead = std::min<DWORD>(available, static_cast<DWORD>(sizeof(buffer)));
        if (!ReadFile(readPipe, buffer, toRead, &bytesRead, nullptr) || bytesRead == 0) {
            break;
        }

        outputBytes.append(buffer, buffer + bytesRead);
    }

    DWORD exitCode = 1;
    if (GetExitCodeProcess(processInfo.hProcess, &exitCode)) {
        result.exitCode = exitCode;
    }

    if (finishedCallback_) {
        finishedCallback_();
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    CloseHandle(readPipe);

    result.output = BytesToWide(outputBytes);
    return result;
}

} // namespace video_cutter_gui
