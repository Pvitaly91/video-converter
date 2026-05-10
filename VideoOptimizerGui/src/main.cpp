#include "ConcatListWriter.h"
#include "FFmpegRunner.h"
#include "Segment.h"
#include "SegmentParser.h"

#include <Windows.h>
#include <CommCtrl.h>
#include <ShlObj.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

using video_optimizer::ConcatListWriter;
using video_optimizer::FFmpegRunner;
using video_optimizer::FormatDuration;
using video_optimizer::ProcessResult;
using video_optimizer::Segment;

constexpr int kWindowWidth = 940;
constexpr int kWindowHeight = 760;

enum ControlId {
    IdInputEdit = 1001,
    IdBrowseInput,
    IdDurationText,
    IdOutputFolderEdit,
    IdBrowseOutputFolder,
    IdManualNameEdit,
    IdStartEdit,
    IdEndEdit,
    IdAddSegment,
    IdSegmentList,
    IdRemoveSegment,
    IdClearSegments,
    IdOverwrite,
    IdKeepTemp,
    IdDryRun,
    IdRun,
    IdLog
};

struct AppState {
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    HWND inputEdit = nullptr;
    HWND durationText = nullptr;
    HWND outputFolderEdit = nullptr;
    HWND manualNameEdit = nullptr;
    HWND startEdit = nullptr;
    HWND endEdit = nullptr;
    HWND segmentList = nullptr;
    HWND overwriteCheck = nullptr;
    HWND keepTempCheck = nullptr;
    HWND dryRunCheck = nullptr;
    HWND runButton = nullptr;
    HWND logEdit = nullptr;
    HFONT font = nullptr;

    std::filesystem::path inputFile;
    std::filesystem::path outputFolder;
    std::vector<Segment> segments;
};

AppState g_app;

std::wstring Widen(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}

std::wstring Trim(std::wstring value) {
    auto isSpace = [](wchar_t ch) {
        return std::iswspace(ch) != 0;
    };

    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), isSpace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), isSpace).base(), value.end());
    return value;
}

std::wstring GetWindowString(HWND handle) {
    const int length = GetWindowTextLengthW(handle);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    if (length > 0) {
        GetWindowTextW(handle, text.data(), length + 1);
    }
    text.resize(static_cast<size_t>(length));
    return text;
}

void SetWindowString(HWND handle, const std::wstring& text) {
    SetWindowTextW(handle, text.c_str());
}

bool IsChecked(HWND handle) {
    return SendMessageW(handle, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

std::wstring UniqueId() {
    const auto ticks = std::chrono::system_clock::now().time_since_epoch().count();
    return std::to_wstring(ticks);
}

std::wstring FormatMegabytes(std::uintmax_t bytes) {
    std::wostringstream stream;
    stream << std::fixed << std::setprecision(2) << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << L" MB";
    return stream.str();
}

void AppendLog(const std::wstring& text) {
    const int length = GetWindowTextLengthW(g_app.logEdit);
    SendMessageW(g_app.logEdit, EM_SETSEL, static_cast<WPARAM>(length), static_cast<LPARAM>(length));
    SendMessageW(g_app.logEdit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
}

void AppendLogLine(const std::wstring& text) {
    AppendLog(text + L"\r\n");
}

void ClearLog() {
    SetWindowString(g_app.logEdit, L"");
}

void ShowError(const std::wstring& message) {
    MessageBoxW(g_app.window, message.c_str(), L"Video Optimizer GUI", MB_ICONERROR | MB_OK);
}

void ApplyFont(HWND handle) {
    SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.font), TRUE);
}

HWND CreateControl(const wchar_t* className,
                   const wchar_t* text,
                   DWORD style,
                   DWORD exStyle,
                   int x,
                   int y,
                   int width,
                   int height,
                   int id) {
    HWND handle = CreateWindowExW(
        exStyle,
        className,
        text,
        style | WS_CHILD | WS_VISIBLE,
        x,
        y,
        width,
        height,
        g_app.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        g_app.instance,
        nullptr);
    ApplyFont(handle);
    return handle;
}

std::filesystem::path MakeSafeOutputPath(const std::filesystem::path& preferred) {
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

    return directory / (stem + L"_" + UniqueId() + extension);
}

bool IsSamePath(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::error_code ec;
    if (std::filesystem::equivalent(left, right, ec) && !ec) {
        return true;
    }

    return _wcsicmp(left.lexically_normal().wstring().c_str(), right.lexically_normal().wstring().c_str()) == 0;
}

std::filesystem::path BuildOutputPath() {
    std::filesystem::path folder = g_app.outputFolder.empty()
        ? g_app.inputFile.parent_path()
        : g_app.outputFolder;

    const std::wstring manualName = Trim(GetWindowString(g_app.manualNameEdit));
    std::filesystem::path output;

    if (!manualName.empty()) {
        std::filesystem::path fileName = std::filesystem::path(manualName).filename();
        if (fileName.extension().empty()) {
            fileName += L".mp4";
        }
        output = folder / fileName;
    } else if (IsSamePath(folder, g_app.inputFile.parent_path())) {
        output = folder / (g_app.inputFile.stem().wstring() + L"_cut_" + UniqueId() + L".mp4");
    } else {
        output = folder / g_app.inputFile.filename();
    }

    std::error_code ec;
    if (std::filesystem::equivalent(output, g_app.inputFile, ec) && !ec) {
        output = output.parent_path() / (output.stem().wstring() + L"_cut_" + UniqueId() + output.extension().wstring());
    }

    if (!IsChecked(g_app.overwriteCheck)) {
        output = MakeSafeOutputPath(output);
    }

    return output;
}

std::wstring TrimProcessOutput(std::wstring output) {
    while (!output.empty() && (output.back() == L'\r' || output.back() == L'\n')) {
        output.pop_back();
    }
    return output;
}

std::vector<std::wstring> BuildCutArguments(const std::filesystem::path& input,
                                            const Segment& segment,
                                            const std::filesystem::path& partFile) {
    return {
        L"-y",
        L"-ss", segment.start,
        L"-to", segment.end,
        L"-i", input.wstring(),
        L"-map", L"0:v:0",
        L"-map", L"0:a?",
        L"-c", L"copy",
        L"-avoid_negative_ts", L"make_zero",
        L"-reset_timestamps", L"1",
        partFile.wstring()
    };
}

std::vector<std::wstring> BuildConcatArguments(const std::filesystem::path& listFile,
                                               const std::filesystem::path& output) {
    return {
        L"-y",
        L"-f", L"concat",
        L"-safe", L"0",
        L"-i", listFile.wstring(),
        L"-c", L"copy",
        output.wstring()
    };
}

std::filesystem::path BuildTempDirectory(const std::filesystem::path& output) {
    const std::filesystem::path base = output.has_parent_path()
        ? output.parent_path()
        : std::filesystem::temp_directory_path();
    return base / (L"video_optimizer_temp_" + UniqueId());
}

std::filesystem::path BuildPartPath(const std::filesystem::path& tempDirectory, size_t index) {
    std::wostringstream name;
    name << L"part_" << std::setfill(L'0') << std::setw(3) << (index + 1) << L".mp4";
    return tempDirectory / name.str();
}

std::int64_t TotalSegmentDuration() {
    std::int64_t total = 0;
    for (const Segment& segment : g_app.segments) {
        total += segment.DurationMilliseconds();
    }
    return total;
}

bool TryParseDuration(const std::wstring& output, std::int64_t& milliseconds) {
    try {
        const long double seconds = std::stold(Trim(output));
        if (seconds < 0) {
            return false;
        }
        milliseconds = static_cast<std::int64_t>(seconds * 1000.0L + 0.5L);
        return true;
    } catch (...) {
        return false;
    }
}

void LoadDuration() {
    SetWindowString(g_app.durationText, L"Тривалість: визначається...");

    FFmpegRunner runner;
    if (!runner.Locate()) {
        SetWindowString(g_app.durationText, L"Тривалість: FFmpeg/FFprobe не знайдено");
        AppendLogLine(runner.MissingToolsMessage());
        return;
    }

    const std::vector<std::wstring> arguments = {
        L"-v", L"error",
        L"-show_entries", L"format=duration",
        L"-of", L"default=noprint_wrappers=1:nokey=1",
        g_app.inputFile.wstring()
    };

    const ProcessResult result = runner.RunFFprobe(arguments);
    if (!result.started || result.exitCode != 0) {
        SetWindowString(g_app.durationText, L"Тривалість: не вдалося визначити");
        AppendLogLine(L"ffprobe duration failed.");
        if (!result.error.empty()) {
            AppendLogLine(result.error);
        }
        if (!result.output.empty()) {
            AppendLogLine(TrimProcessOutput(result.output));
        }
        return;
    }

    std::int64_t milliseconds = 0;
    if (!TryParseDuration(result.output, milliseconds)) {
        SetWindowString(g_app.durationText, L"Тривалість: не вдалося розпарсити");
        AppendLogLine(L"ffprobe returned unsupported duration: " + TrimProcessOutput(result.output));
        return;
    }

    SetWindowString(g_app.durationText, L"Тривалість: " + FormatDuration(milliseconds));
}

bool PickInputFile() {
    wchar_t fileName[MAX_PATH]{};

    OPENFILENAMEW openFile{};
    openFile.lStructSize = sizeof(openFile);
    openFile.hwndOwner = g_app.window;
    openFile.lpstrFile = fileName;
    openFile.nMaxFile = MAX_PATH;
    openFile.lpstrFilter = L"MP4 video (*.mp4)\0*.mp4\0All files (*.*)\0*.*\0";
    openFile.nFilterIndex = 1;
    openFile.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&openFile)) {
        return false;
    }

    g_app.inputFile = std::filesystem::path(fileName);
    g_app.outputFolder = g_app.inputFile.parent_path();
    SetWindowString(g_app.inputEdit, g_app.inputFile.wstring());
    SetWindowString(g_app.outputFolderEdit, g_app.outputFolder.wstring());
    ClearLog();
    AppendLogLine(L"Обрано файл: " + g_app.inputFile.wstring());
    LoadDuration();
    return true;
}

bool PickOutputFolder() {
    BROWSEINFOW browse{};
    browse.hwndOwner = g_app.window;
    browse.lpszTitle = L"Оберіть папку для результату";
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    PIDLIST_ABSOLUTE itemList = SHBrowseForFolderW(&browse);
    if (itemList == nullptr) {
        return false;
    }

    wchar_t folder[MAX_PATH]{};
    const BOOL ok = SHGetPathFromIDListW(itemList, folder);
    CoTaskMemFree(itemList);
    if (!ok) {
        return false;
    }

    g_app.outputFolder = std::filesystem::path(folder);
    SetWindowString(g_app.outputFolderEdit, g_app.outputFolder.wstring());
    AppendLogLine(L"Папка результату: " + g_app.outputFolder.wstring());
    return true;
}

void RefreshSegmentsList() {
    SendMessageW(g_app.segmentList, LB_RESETCONTENT, 0, 0);
    for (size_t index = 0; index < g_app.segments.size(); ++index) {
        const Segment& segment = g_app.segments[index];
        std::wostringstream line;
        line << (index + 1) << L". " << segment.start << L" - " << segment.end
             << L" (" << FormatDuration(segment.DurationMilliseconds()) << L")";
        SendMessageW(g_app.segmentList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.str().c_str()));
    }
}

void AddSegmentFromInputs() {
    const std::wstring start = Trim(GetWindowString(g_app.startEdit));
    const std::wstring end = Trim(GetWindowString(g_app.endEdit));

    std::vector<Segment> parsed;
    std::wstring error;
    if (!video_optimizer::ParseSegmentList(start + L"-" + end, parsed, error)) {
        ShowError(error + L"\nПриклад: 1:00-2:00 або 00:01:00.500-00:02:10.000");
        return;
    }

    g_app.segments.push_back(parsed.front());
    SetWindowString(g_app.startEdit, L"");
    SetWindowString(g_app.endEdit, L"");
    RefreshSegmentsList();
}

void RemoveSelectedSegment() {
    const LRESULT selected = SendMessageW(g_app.segmentList, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        return;
    }

    const size_t index = static_cast<size_t>(selected);
    if (index < g_app.segments.size()) {
        g_app.segments.erase(g_app.segments.begin() + index);
        RefreshSegmentsList();
    }
}

bool ValidateBeforeRun(std::filesystem::path& output) {
    if (g_app.inputFile.empty()) {
        ShowError(L"Спочатку оберіть MP4-файл.");
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(g_app.inputFile, ec) || ec) {
        ShowError(L"Вхідний файл не існує або недоступний.");
        return false;
    }

    if (g_app.segments.empty()) {
        ShowError(L"Додайте хоча б один інтервал.");
        return false;
    }

    output = BuildOutputPath();
    if (output.empty()) {
        ShowError(L"Не вдалося сформувати шлях результату.");
        return false;
    }

    if (IsSamePath(output, g_app.inputFile)) {
        ShowError(L"Результат не може мати той самий шлях, що й оригінал.");
        return false;
    }

    return true;
}

void PumpUi() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void RunCut() {
    std::filesystem::path output;
    if (!ValidateBeforeRun(output)) {
        return;
    }

    ClearLog();
    AppendLogLine(L"Input:  " + g_app.inputFile.wstring());
    AppendLogLine(L"Output: " + output.wstring());
    AppendLogLine(L"Intervals: " + std::to_wstring(g_app.segments.size()));
    AppendLogLine(L"Total duration: " + FormatDuration(TotalSegmentDuration()));
    AppendLogLine(L"");

    FFmpegRunner runner;
    const bool ffmpegFound = runner.Locate();
    const bool dryRun = IsChecked(g_app.dryRunCheck);
    if (!ffmpegFound && !dryRun) {
        ShowError(runner.MissingToolsMessage());
        AppendLogLine(runner.MissingToolsMessage());
        return;
    }
    if (!ffmpegFound) {
        AppendLogLine(runner.MissingToolsMessage());
        AppendLogLine(L"Dry-run: команди будуть показані без запуску FFmpeg.");
        AppendLogLine(L"");
    }

    const std::filesystem::path tempDirectory = BuildTempDirectory(output);
    std::vector<std::filesystem::path> partFiles;
    for (size_t index = 0; index < g_app.segments.size(); ++index) {
        partFiles.push_back(BuildPartPath(tempDirectory, index));
    }
    const std::filesystem::path listFile = tempDirectory / L"list.txt";

    for (size_t index = 0; index < g_app.segments.size(); ++index) {
        AppendLogLine(L"Part " + std::to_wstring(index + 1) + L": "
            + g_app.segments[index].start + L"-" + g_app.segments[index].end);
        AppendLogLine(runner.BuildFFmpegCommand(BuildCutArguments(g_app.inputFile, g_app.segments[index], partFiles[index])));
        AppendLogLine(L"");
    }

    AppendLogLine(L"Concat:");
    AppendLogLine(runner.BuildFFmpegCommand(BuildConcatArguments(listFile, output)));
    AppendLogLine(L"");

    if (dryRun) {
        AppendLogLine(L"Dry-run complete. Файли не створювались.");
        return;
    }

    EnableWindow(g_app.runButton, FALSE);

    std::error_code ec;
    std::filesystem::create_directories(output.parent_path(), ec);
    if (ec) {
        AppendLogLine(L"ERROR: cannot create output folder: " + Widen(ec.message()));
        EnableWindow(g_app.runButton, TRUE);
        return;
    }

    std::filesystem::create_directories(tempDirectory, ec);
    if (ec) {
        AppendLogLine(L"ERROR: cannot create temp folder: " + Widen(ec.message()));
        EnableWindow(g_app.runButton, TRUE);
        return;
    }

    bool ok = true;
    std::wstring error;

    for (size_t index = 0; index < g_app.segments.size(); ++index) {
        AppendLogLine(L"Cutting part " + std::to_wstring(index + 1) + L"/" + std::to_wstring(g_app.segments.size()) + L"...");
        PumpUi();

        const ProcessResult result = runner.RunFFmpeg(BuildCutArguments(g_app.inputFile, g_app.segments[index], partFiles[index]));
        if (!result.started || result.exitCode != 0) {
            ok = false;
            error = L"ffmpeg failed while cutting part " + std::to_wstring(index + 1);
            if (!result.error.empty()) {
                error += L"\r\n" + result.error;
            }
            if (!result.output.empty()) {
                error += L"\r\n" + TrimProcessOutput(result.output);
            }
            break;
        }
    }

    if (ok) {
        if (!ConcatListWriter::Write(listFile, partFiles, error)) {
            ok = false;
        }
    }

    if (ok) {
        AppendLogLine(L"Concatenating parts...");
        PumpUi();
        const ProcessResult result = runner.RunFFmpeg(BuildConcatArguments(listFile, output));
        if (!result.started || result.exitCode != 0) {
            ok = false;
            error = L"ffmpeg failed while concatenating.";
            if (!result.error.empty()) {
                error += L"\r\n" + result.error;
            }
            if (!result.output.empty()) {
                error += L"\r\n" + TrimProcessOutput(result.output);
            }
        }
    }

    bool tempDeleted = false;
    if (ok && !IsChecked(g_app.keepTempCheck)) {
        std::filesystem::remove_all(tempDirectory, ec);
        tempDeleted = !ec;
        if (ec) {
            ok = false;
            error = L"Output was created, but temp folder was not removed: " + Widen(ec.message());
        }
    }

    const std::uintmax_t inputSize = std::filesystem::file_size(g_app.inputFile, ec);
    const std::uintmax_t safeInputSize = ec ? 0 : inputSize;
    ec.clear();
    std::uintmax_t outputSize = 0;
    if (std::filesystem::exists(output, ec) && !ec) {
        outputSize = std::filesystem::file_size(output, ec);
        if (ec) {
            outputSize = 0;
        }
    }

    AppendLogLine(L"");
    AppendLogLine(L"Summary");
    AppendLogLine(L"Status: " + std::wstring(ok ? L"OK" : L"ERROR"));
    AppendLogLine(L"Input size:  " + FormatMegabytes(safeInputSize));
    AppendLogLine(L"Output size: " + FormatMegabytes(outputSize));
    AppendLogLine(L"Temp: " + std::wstring(IsChecked(g_app.keepTempCheck) ? L"kept" : (tempDeleted ? L"deleted" : L"kept")) + L" (" + tempDirectory.wstring() + L")");
    if (!error.empty()) {
        AppendLogLine(L"Error:");
        AppendLogLine(error);
    }

    EnableWindow(g_app.runButton, TRUE);

    MessageBoxW(g_app.window,
                ok ? L"Готово. Результат створено." : L"Сталася помилка. Деталі в логах.",
                L"Video Optimizer GUI",
                ok ? MB_ICONINFORMATION : MB_ICONERROR);
}

void CreateUi() {
    g_app.font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    CreateControl(L"STATIC", L"MP4-файл:", WS_TABSTOP, 0, 16, 16, 90, 22, 0);
    g_app.inputEdit = CreateControl(L"EDIT", L"", ES_AUTOHSCROLL | ES_READONLY, WS_EX_CLIENTEDGE, 112, 14, 650, 24, IdInputEdit);
    CreateControl(L"BUTTON", L"Обрати...", BS_PUSHBUTTON, 0, 776, 12, 130, 28, IdBrowseInput);
    g_app.durationText = CreateControl(L"STATIC", L"Тривалість: файл не обрано", 0, 0, 112, 46, 650, 22, IdDurationText);

    CreateControl(L"STATIC", L"Папка результату:", 0, 0, 16, 82, 120, 22, 0);
    g_app.outputFolderEdit = CreateControl(L"EDIT", L"", ES_AUTOHSCROLL | ES_READONLY, WS_EX_CLIENTEDGE, 144, 80, 618, 24, IdOutputFolderEdit);
    CreateControl(L"BUTTON", L"Обрати...", BS_PUSHBUTTON, 0, 776, 78, 130, 28, IdBrowseOutputFolder);

    CreateControl(L"STATIC", L"Назва файлу вручну:", 0, 0, 16, 120, 140, 22, 0);
    g_app.manualNameEdit = CreateControl(L"EDIT", L"", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 160, 118, 360, 24, IdManualNameEdit);
    CreateControl(L"STATIC", L"(необов'язково; .mp4 додасться автоматично)", 0, 0, 532, 120, 360, 22, 0);

    CreateControl(L"STATIC", L"Інтервал:", 0, 0, 16, 164, 80, 22, 0);
    CreateControl(L"STATIC", L"Початок", 0, 0, 112, 148, 90, 18, 0);
    g_app.startEdit = CreateControl(L"EDIT", L"", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 112, 166, 130, 24, IdStartEdit);
    CreateControl(L"STATIC", L"Кінець", 0, 0, 260, 148, 90, 18, 0);
    g_app.endEdit = CreateControl(L"EDIT", L"", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 260, 166, 130, 24, IdEndEdit);
    CreateControl(L"BUTTON", L"Додати", BS_PUSHBUTTON, 0, 408, 164, 100, 28, IdAddSegment);
    CreateControl(L"STATIC", L"Формати: SS, MM:SS, HH:MM:SS, HH:MM:SS.mmm", 0, 0, 526, 168, 380, 22, 0);

    g_app.segmentList = CreateControl(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_BORDER, WS_EX_CLIENTEDGE, 112, 204, 650, 132, IdSegmentList);
    CreateControl(L"BUTTON", L"Видалити", BS_PUSHBUTTON, 0, 776, 204, 130, 28, IdRemoveSegment);
    CreateControl(L"BUTTON", L"Очистити", BS_PUSHBUTTON, 0, 776, 238, 130, 28, IdClearSegments);

    g_app.overwriteCheck = CreateControl(L"BUTTON", L"Перезаписати результат, якщо існує", BS_AUTOCHECKBOX, 0, 112, 350, 250, 24, IdOverwrite);
    g_app.keepTempCheck = CreateControl(L"BUTTON", L"Залишити тимчасові part-файли", BS_AUTOCHECKBOX, 0, 380, 350, 240, 24, IdKeepTemp);
    g_app.dryRunCheck = CreateControl(L"BUTTON", L"Dry-run", BS_AUTOCHECKBOX, 0, 638, 350, 90, 24, IdDryRun);
    g_app.runButton = CreateControl(L"BUTTON", L"Створити MP4", BS_DEFPUSHBUTTON, 0, 776, 346, 130, 32, IdRun);

    CreateControl(L"STATIC", L"Лог:", 0, 0, 16, 398, 80, 22, 0);
    g_app.logEdit = CreateControl(
        L"EDIT",
        L"",
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        WS_EX_CLIENTEDGE,
        112,
        398,
        794,
        300,
        IdLog);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        g_app.window = window;
        CreateUi();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IdBrowseInput:
            PickInputFile();
            return 0;
        case IdBrowseOutputFolder:
            PickOutputFolder();
            return 0;
        case IdAddSegment:
            AddSegmentFromInputs();
            return 0;
        case IdRemoveSegment:
            RemoveSelectedSegment();
            return 0;
        case IdClearSegments:
            g_app.segments.clear();
            RefreshSegmentsList();
            return 0;
        case IdRun:
            RunCut();
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int commandShow) {
    g_app.instance = instance;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&controls);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = L"VideoOptimizerGuiWindow";

    RegisterClassExW(&windowClass);

    HWND window = CreateWindowExW(
        0,
        windowClass.lpszClassName,
        L"Video Optimizer - вирізання сегментів",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kWindowWidth,
        kWindowHeight,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (window == nullptr) {
        CoUninitialize();
        return 1;
    }

    ShowWindow(window, commandShow);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    CoUninitialize();
    return static_cast<int>(message.wParam);
}
