#include "MainWindow.h"

#include "OutputPathBuilder.h"
#include "TimeParser.h"
#include "VideoDurationProbe.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <CommCtrl.h>
#include <ShObjIdl.h>
#include <Shellapi.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>

namespace video_cutter_gui {
namespace {

constexpr int kWindowWidth = 1130;
constexpr int kWindowHeight = 860;

constexpr UINT WM_APP_LOG_LINE = WM_APP + 1;
constexpr UINT WM_APP_PROGRESS = WM_APP + 2;
constexpr UINT WM_APP_PROBE_DONE = WM_APP + 3;
constexpr UINT WM_APP_EXPORT_DONE = WM_APP + 4;

enum ControlId {
    IdInputEdit = 1001,
    IdBrowseInput,
    IdMetadataEdit,
    IdStartEdit,
    IdEndEdit,
    IdAddInterval,
    IdIntervalList,
    IdRemoveInterval,
    IdClearIntervals,
    IdMoveUp,
    IdMoveDown,
    IdOutputFolderEdit,
    IdBrowseOutputFolder,
    IdManualNameEdit,
    IdOutputPreviewEdit,
    IdExport,
    IdDryRun,
    IdCancel,
    IdOpenOutput,
    IdProgress,
    IdStatus,
    IdLog
};

template <typename T>
void SafeRelease(T*& value) {
    if (value != nullptr) {
        value->Release();
        value = nullptr;
    }
}

void SetCueBanner(HWND control, const wchar_t* text) {
    SendMessageW(control, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(text));
}

std::wstring BrowseFile(HWND owner) {
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) {
        return {};
    }

    COMDLG_FILTERSPEC filters[] = {
        { L"MP4 video (*.mp4)", L"*.mp4" }
    };
    dialog->SetFileTypes(1, filters);
    dialog->SetDefaultExtension(L"mp4");

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);

    std::wstring path;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR rawPath = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath))) {
                path = rawPath;
                CoTaskMemFree(rawPath);
            }
            SafeRelease(item);
        }
    }

    SafeRelease(dialog);
    return path;
}

std::wstring BrowseFolder(HWND owner) {
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) {
        return {};
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Choose output folder");

    std::wstring path;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR rawPath = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath))) {
                path = rawPath;
                CoTaskMemFree(rawPath);
            }
            SafeRelease(item);
        }
    }

    SafeRelease(dialog);
    return path;
}

void AddListViewColumn(HWND list, int index, int width, const wchar_t* title) {
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.pszText = const_cast<LPWSTR>(title);
    column.cx = width;
    column.iSubItem = index;
    ListView_InsertColumn(list, index, &column);
}

std::wstring InfoText(const VideoInfo& info) {
    if (!info.ok) {
        return info.error.empty() ? L"No video loaded." : info.error;
    }

    std::wostringstream stream;
    stream << L"Duration: " << FormatTime(info.durationMilliseconds);

    if (info.width > 0 && info.height > 0) {
        stream << L"   Resolution: " << info.width << L"x" << info.height;
    }

    stream << L"   Video codec: " << (info.videoCodec.empty() ? L"unknown" : info.videoCodec)
           << L"   Audio codec: " << (info.audioCodec.empty() ? L"none" : info.audioCodec);
    return stream.str();
}

bool IsMp4File(const std::filesystem::path& path) {
    return _wcsicmp(path.extension().wstring().c_str(), L".mp4") == 0;
}

} // namespace

bool MainWindow::Create(HINSTANCE instance, int commandShow) {
    instance_ = instance;
    font_ = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = MainWindow::WindowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = L"VideoCutterGuiWindow";

    if (RegisterClassExW(&windowClass) == 0) {
        return false;
    }

    HWND window = CreateWindowExW(
        0,
        windowClass.lpszClassName,
        L"Video Cutter GUI - MP4 Segment Export",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kWindowWidth,
        kWindowHeight,
        nullptr,
        nullptr,
        instance_,
        this);

    if (window == nullptr) {
        return false;
    }

    ShowWindow(window, commandShow);
    UpdateWindow(window);
    return true;
}

int MainWindow::RunMessageLoop() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK MainWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<MainWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (self != nullptr) {
        return self->HandleMessage(message, wParam, lParam);
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateControls();
        UpdateControls();
        return 0;

    case WM_COMMAND:
        if (HIWORD(wParam) == EN_CHANGE
            && (LOWORD(wParam) == IdManualNameEdit || LOWORD(wParam) == IdOutputFolderEdit)) {
            UpdateOutputPreview();
            return 0;
        }

        switch (LOWORD(wParam)) {
        case IdBrowseInput:
            PickInputVideo();
            return 0;
        case IdBrowseOutputFolder:
            PickOutputFolder();
            return 0;
        case IdAddInterval:
            AddInterval();
            return 0;
        case IdRemoveInterval:
            RemoveSelectedInterval();
            return 0;
        case IdClearIntervals:
            ClearIntervals();
            return 0;
        case IdMoveUp:
            MoveSelectedInterval(-1);
            return 0;
        case IdMoveDown:
            MoveSelectedInterval(1);
            return 0;
        case IdExport:
            StartExport(false);
            return 0;
        case IdDryRun:
            StartExport(true);
            return 0;
        case IdCancel:
            CancelExport();
            return 0;
        case IdOpenOutput:
            OpenOutputFolder();
            return 0;
        default:
            break;
        }
        break;

    case WM_NOTIFY:
        if (reinterpret_cast<NMHDR*>(lParam)->idFrom == IdIntervalList) {
            UpdateControls();
        }
        break;

    case WM_APP_LOG_LINE: {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));
        AppendLogLine(*text);
        return 0;
    }

    case WM_APP_PROGRESS: {
        std::unique_ptr<ExportProgress> progress(reinterpret_cast<ExportProgress*>(lParam));
        SendMessageW(progressBar_, PBM_SETPOS, progress->percent, 0);
        SetStatus(progress->message);
        return 0;
    }

    case WM_APP_PROBE_DONE: {
        std::unique_ptr<VideoInfo> info(reinterpret_cast<VideoInfo*>(lParam));
        HandleProbeResult(static_cast<int>(wParam), std::move(info));
        return 0;
    }

    case WM_APP_EXPORT_DONE: {
        std::unique_ptr<ExportResult> result(reinterpret_cast<ExportResult*>(lParam));
        HandleExportDone(std::move(result));
        return 0;
    }

    case WM_DESTROY:
        if (activeExporter_) {
            activeExporter_->Cancel();
            activeExporter_.reset();
        }
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(window_, message, wParam, lParam);
}

void MainWindow::CreateControls() {
    CreateLabel(L"Input MP4:", 18, 18, 120, 22);
    inputEdit_ = CreateControl(L"EDIT", L"", ES_AUTOHSCROLL | ES_READONLY, WS_EX_CLIENTEDGE, 150, 16, 760, 24, IdInputEdit);
    browseInputButton_ = CreateControl(L"BUTTON", L"Вибрати відео", BS_PUSHBUTTON, 0, 930, 14, 160, 28, IdBrowseInput);

    CreateLabel(L"Video info:", 18, 56, 120, 22);
    metadataEdit_ = CreateControl(
        L"EDIT",
        L"No video loaded.",
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        WS_EX_CLIENTEDGE,
        150,
        52,
        940,
        46,
        IdMetadataEdit);

    CreateLabel(L"Start:", 18, 126, 80, 22);
    startEdit_ = CreateControl(L"EDIT", L"", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 150, 122, 150, 24, IdStartEdit);
    SetCueBanner(startEdit_, L"SS / MM:SS / HH:MM:SS");

    CreateLabel(L"End:", 318, 126, 80, 22);
    endEdit_ = CreateControl(L"EDIT", L"", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 370, 122, 150, 24, IdEndEdit);
    SetCueBanner(endEdit_, L"HH:MM:SS.mmm");

    addButton_ = CreateControl(L"BUTTON", L"Додати інтервал", BS_PUSHBUTTON, 0, 540, 119, 150, 30, IdAddInterval);
    CreateLabel(L"Formats: 90, 1:30, 01:30, 1:02:03, 00:01:00.500", 710, 126, 380, 22);

    intervalList_ = CreateControl(
        WC_LISTVIEWW,
        L"",
        LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        WS_EX_CLIENTEDGE,
        18,
        166,
        800,
        180,
        IdIntervalList);
    ListView_SetExtendedListViewStyle(intervalList_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    AddListViewColumn(intervalList_, 0, 70, L"#");
    AddListViewColumn(intervalList_, 1, 190, L"Start");
    AddListViewColumn(intervalList_, 2, 190, L"End");
    AddListViewColumn(intervalList_, 3, 190, L"Duration");

    removeButton_ = CreateControl(L"BUTTON", L"Remove selected", BS_PUSHBUTTON, 0, 840, 166, 250, 30, IdRemoveInterval);
    clearButton_ = CreateControl(L"BUTTON", L"Clear all", BS_PUSHBUTTON, 0, 840, 202, 250, 30, IdClearIntervals);
    moveUpButton_ = CreateControl(L"BUTTON", L"Move up", BS_PUSHBUTTON, 0, 840, 238, 120, 30, IdMoveUp);
    moveDownButton_ = CreateControl(L"BUTTON", L"Move down", BS_PUSHBUTTON, 0, 970, 238, 120, 30, IdMoveDown);

    CreateLabel(L"Output folder:", 18, 384, 120, 22);
    outputFolderEdit_ = CreateControl(L"EDIT", L"", ES_AUTOHSCROLL | ES_READONLY, WS_EX_CLIENTEDGE, 150, 380, 760, 24, IdOutputFolderEdit);
    browseOutputButton_ = CreateControl(L"BUTTON", L"Вибрати папку", BS_PUSHBUTTON, 0, 930, 377, 160, 30, IdBrowseOutputFolder);

    CreateLabel(L"Manual name:", 18, 426, 120, 22);
    manualNameEdit_ = CreateControl(L"EDIT", L"", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 150, 422, 300, 24, IdManualNameEdit);
    SetCueBanner(manualNameEdit_, L"optional-name.mp4");
    CreateLabel(L"Empty: another folder keeps original name; same folder gets _cut timestamp.", 470, 426, 620, 22);

    CreateLabel(L"Output preview:", 18, 468, 120, 22);
    outputPreviewEdit_ = CreateControl(L"EDIT", L"", ES_AUTOHSCROLL | ES_READONLY, WS_EX_CLIENTEDGE, 150, 464, 940, 24, IdOutputPreviewEdit);

    exportButton_ = CreateControl(L"BUTTON", L"Start / Export", BS_DEFPUSHBUTTON, 0, 150, 510, 150, 34, IdExport);
    dryRunButton_ = CreateControl(L"BUTTON", L"Dry Run", BS_PUSHBUTTON, 0, 316, 510, 120, 34, IdDryRun);
    cancelButton_ = CreateControl(L"BUTTON", L"Cancel", BS_PUSHBUTTON, 0, 452, 510, 120, 34, IdCancel);
    openOutputButton_ = CreateControl(L"BUTTON", L"Open output folder", BS_PUSHBUTTON, 0, 588, 510, 180, 34, IdOpenOutput);

    progressBar_ = CreateControl(PROGRESS_CLASSW, L"", 0, 0, 150, 560, 760, 22, IdProgress);
    SendMessageW(progressBar_, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    statusText_ = CreateLabel(L"Ready.", 930, 560, 160, 22);

    CreateLabel(L"Fast mode uses FFmpeg -c copy. MP4/H.264 cut points may align to keyframes; exact frame cuts need a future re-encode mode.", 18, 596, 1072, 22);

    CreateLabel(L"Log:", 18, 626, 120, 22);
    logEdit_ = CreateControl(
        L"EDIT",
        L"",
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        WS_EX_CLIENTEDGE,
        18,
        650,
        1072,
        150,
        IdLog);
}

HWND MainWindow::CreateControl(const wchar_t* className,
                               const wchar_t* text,
                               DWORD style,
                               DWORD exStyle,
                               int x,
                               int y,
                               int width,
                               int height,
                               int id) {
    HWND control = CreateWindowExW(
        exStyle,
        className,
        text,
        style | WS_CHILD | WS_VISIBLE,
        x,
        y,
        width,
        height,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        instance_,
        nullptr);
    ApplyFont(control);
    return control;
}

HWND MainWindow::CreateLabel(const wchar_t* text, int x, int y, int width, int height) {
    return CreateControl(L"STATIC", text, 0, 0, x, y, width, height, 0);
}

void MainWindow::ApplyFont(HWND control) const {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
}

void MainWindow::PickInputVideo() {
    const std::wstring pathText = BrowseFile(window_);
    if (pathText.empty()) {
        return;
    }

    const std::filesystem::path inputPath(pathText);
    if (!IsMp4File(inputPath)) {
        ShowError(L"Only .mp4 files are supported by this GUI.");
        return;
    }

    state_.inputFile = inputPath;
    state_.outputFolder = inputPath.parent_path();
    state_.previewOutputFile.clear();
    state_.lastOutputFile.clear();
    state_.segmentEditor.Clear();
    state_.videoInfo = VideoInfo{};

    SetText(inputEdit_, state_.inputFile.wstring());
    SetText(outputFolderEdit_, state_.outputFolder.wstring());
    SetText(metadataEdit_, L"Probing video...");
    SetText(manualNameEdit_, L"");
    RefreshIntervalList();
    ClearLog();
    AppendLogLine(L"Selected video: " + state_.inputFile.wstring());
    UpdateOutputPreview();
    UpdateControls();
    StartProbe();
}

void MainWindow::PickOutputFolder() {
    const std::wstring folderText = BrowseFolder(window_);
    if (folderText.empty()) {
        return;
    }

    state_.outputFolder = std::filesystem::path(folderText);
    SetText(outputFolderEdit_, state_.outputFolder.wstring());
    AppendLogLine(L"Output folder: " + state_.outputFolder.wstring());
    UpdateOutputPreview();
}

void MainWindow::StartProbe() {
    const int generation = ++probeGeneration_;
    const std::filesystem::path input = state_.inputFile;
    SetStatus(L"probing");

    std::thread([window = window_, input, generation]() {
        VideoDurationProbe probe;
        auto info = std::make_unique<VideoInfo>(probe.Probe(input));
        PostMessageW(window, WM_APP_PROBE_DONE, static_cast<WPARAM>(generation), reinterpret_cast<LPARAM>(info.release()));
    }).detach();
}

void MainWindow::HandleProbeResult(int generation, std::unique_ptr<VideoInfo> info) {
    if (generation != probeGeneration_.load()) {
        return;
    }

    state_.videoInfo = *info;
    SetText(metadataEdit_, InfoText(state_.videoInfo));

    if (state_.videoInfo.tools.IsComplete()) {
        AppendLogLine(L"FFmpeg:  " + state_.videoInfo.tools.ffmpegPath.wstring());
        AppendLogLine(L"FFprobe: " + state_.videoInfo.tools.ffprobePath.wstring());
    }

    if (state_.videoInfo.ok) {
        AppendLogLine(L"Duration: " + FormatTime(state_.videoInfo.durationMilliseconds));
        AppendLogLine(L"Video: " + state_.videoInfo.videoCodec + L", audio: " + state_.videoInfo.audioCodec);
        SetStatus(L"ready");
    } else {
        AppendLogLine(L"Probe error: " + state_.videoInfo.error);
        SetStatus(L"probe failed");
        ShowError(state_.videoInfo.error);
    }

    UpdateControls();
}

void MainWindow::AddInterval() {
    if (state_.inputFile.empty()) {
        ShowError(L"Choose an MP4 video first.");
        return;
    }

    std::optional<std::int64_t> knownDuration;
    if (state_.videoInfo.ok) {
        knownDuration = state_.videoInfo.durationMilliseconds;
    }

    std::wstring error;
    if (!state_.segmentEditor.Add(GetText(startEdit_), GetText(endEdit_), knownDuration, error)) {
        ShowError(error);
        return;
    }

    SetText(startEdit_, L"");
    SetText(endEdit_, L"");
    RefreshIntervalList(static_cast<int>(state_.segmentEditor.Size()) - 1);
    UpdateControls();
}

void MainWindow::RemoveSelectedInterval() {
    const int index = SelectedIntervalIndex();
    if (index < 0) {
        return;
    }

    state_.segmentEditor.Remove(static_cast<size_t>(index));
    RefreshIntervalList(std::min<int>(index, static_cast<int>(state_.segmentEditor.Size()) - 1));
    UpdateControls();
}

void MainWindow::MoveSelectedInterval(int direction) {
    const int index = SelectedIntervalIndex();
    if (index < 0) {
        return;
    }

    int newIndex = index;
    if (direction < 0 && state_.segmentEditor.MoveUp(static_cast<size_t>(index))) {
        newIndex = index - 1;
    } else if (direction > 0 && state_.segmentEditor.MoveDown(static_cast<size_t>(index))) {
        newIndex = index + 1;
    }

    RefreshIntervalList(newIndex);
    UpdateControls();
}

void MainWindow::ClearIntervals() {
    state_.segmentEditor.Clear();
    RefreshIntervalList();
    UpdateControls();
}

void MainWindow::RefreshIntervalList(int selectIndex) {
    ListView_DeleteAllItems(intervalList_);

    const auto& segments = state_.segmentEditor.Segments();
    for (size_t index = 0; index < segments.size(); ++index) {
        const SegmentInterval& segment = segments[index];
        const std::wstring number = std::to_wstring(index + 1);
        const std::wstring duration = FormatTime(segment.DurationMilliseconds());

        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(index);
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(number.c_str());
        ListView_InsertItem(intervalList_, &item);
        ListView_SetItemText(intervalList_, static_cast<int>(index), 1, const_cast<LPWSTR>(segment.startText.c_str()));
        ListView_SetItemText(intervalList_, static_cast<int>(index), 2, const_cast<LPWSTR>(segment.endText.c_str()));
        ListView_SetItemText(intervalList_, static_cast<int>(index), 3, const_cast<LPWSTR>(duration.c_str()));
    }

    if (selectIndex >= 0 && selectIndex < static_cast<int>(segments.size())) {
        ListView_SetItemState(intervalList_, selectIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}

int MainWindow::SelectedIntervalIndex() const {
    return ListView_GetNextItem(intervalList_, -1, LVNI_SELECTED);
}

void MainWindow::UpdateOutputPreview() {
    if (state_.inputFile.empty()) {
        state_.previewOutputFile.clear();
        SetText(outputPreviewEdit_, L"");
        return;
    }

    OutputPathRequest request;
    request.inputFile = state_.inputFile;
    request.outputFolder = state_.outputFolder;
    request.manualFileName = GetText(manualNameEdit_);

    const OutputPathResult output = OutputPathBuilder::Build(request);
    state_.previewOutputFile = output.outputFile;
    SetText(outputPreviewEdit_, state_.previewOutputFile.wstring());
}

void MainWindow::StartExport(bool dryRun) {
    if (state_.busy) {
        return;
    }

    if (state_.inputFile.empty()) {
        ShowError(L"Choose an MP4 video first.");
        return;
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(state_.inputFile, ec) || ec) {
        ShowError(L"Input file does not exist or is not accessible.");
        return;
    }

    if (state_.segmentEditor.Empty()) {
        ShowError(L"Add at least one interval.");
        return;
    }

    OutputPathRequest request;
    request.inputFile = state_.inputFile;
    request.outputFolder = state_.outputFolder;
    request.manualFileName = GetText(manualNameEdit_);
    const OutputPathResult output = OutputPathBuilder::Build(request);
    state_.previewOutputFile = output.outputFile;
    SetText(outputPreviewEdit_, state_.previewOutputFile.wstring());

    if (PathsEqual(state_.previewOutputFile, state_.inputFile)) {
        ShowError(L"Output path cannot be the same as the input file.");
        return;
    }

    ClearLog();
    if (!output.note.empty()) {
        AppendLogLine(output.note);
    }

    ExportSettings settings;
    settings.inputFile = state_.inputFile;
    settings.outputFile = state_.previewOutputFile;
    settings.segments = state_.segmentEditor.Segments();
    settings.dryRun = dryRun;
    settings.keepTemp = false;

    SetBusy(true);
    SendMessageW(progressBar_, PBM_SETPOS, 0, 0);
    SetStatus(dryRun ? L"dry run" : L"exporting");

    auto exporter = std::make_shared<VideoSegmentExporter>(
        [window = window_](const std::wstring& text) {
            auto* copy = new std::wstring(text);
            PostMessageW(window, WM_APP_LOG_LINE, 0, reinterpret_cast<LPARAM>(copy));
        },
        [window = window_](const ExportProgress& progress) {
            auto* copy = new ExportProgress(progress);
            PostMessageW(window, WM_APP_PROGRESS, 0, reinterpret_cast<LPARAM>(copy));
        });

    activeExporter_ = exporter;

    std::thread([window = window_, exporter, settings]() {
        auto result = std::make_unique<ExportResult>(exporter->Export(settings));
        PostMessageW(window, WM_APP_EXPORT_DONE, 0, reinterpret_cast<LPARAM>(result.release()));
    }).detach();
}

void MainWindow::CancelExport() {
    if (!activeExporter_) {
        return;
    }

    AppendLogLine(L"Cancel requested.");
    SetStatus(L"canceling");
    activeExporter_->Cancel();
}

void MainWindow::HandleExportDone(std::unique_ptr<ExportResult> result) {
    activeExporter_.reset();
    SetBusy(false);

    if (result->success && !result->dryRun) {
        state_.lastOutputFile = result->outputFile;
        SetStatus(L"done");
        SendMessageW(progressBar_, PBM_SETPOS, 100, 0);
        MessageBoxW(window_, L"Export complete. Output passed ffprobe validation.", L"Video Cutter GUI", MB_ICONINFORMATION | MB_OK);
    } else if (result->success && result->dryRun) {
        SetStatus(L"dry run complete");
        MessageBoxW(window_, L"Dry Run complete. Commands are in the log.", L"Video Cutter GUI", MB_ICONINFORMATION | MB_OK);
    } else if (result->canceled) {
        SetStatus(L"canceled");
        MessageBoxW(window_, L"Export canceled.", L"Video Cutter GUI", MB_ICONWARNING | MB_OK);
    } else {
        SetStatus(L"error");
        ShowError(result->error.empty() ? L"Export failed. See log for details." : result->error);
    }

    UpdateControls();
}

void MainWindow::OpenOutputFolder() {
    std::filesystem::path folder;
    if (!state_.lastOutputFile.empty()) {
        folder = state_.lastOutputFile.parent_path();
    } else if (!state_.previewOutputFile.empty()) {
        folder = state_.previewOutputFile.parent_path();
    } else if (!state_.inputFile.empty()) {
        folder = state_.inputFile.parent_path();
    }

    if (folder.empty()) {
        ShowError(L"No output folder is available yet.");
        return;
    }

    HINSTANCE result = ShellExecuteW(window_, L"open", folder.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        ShowError(L"Could not open output folder:\n" + folder.wstring());
    }
}

void MainWindow::AppendLogLine(const std::wstring& text) {
    if (logEdit_ == nullptr) {
        return;
    }

    const int length = GetWindowTextLengthW(logEdit_);
    SendMessageW(logEdit_, EM_SETSEL, static_cast<WPARAM>(length), static_cast<LPARAM>(length));
    const std::wstring line = text + L"\r\n";
    SendMessageW(logEdit_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
}

void MainWindow::ClearLog() {
    SetText(logEdit_, L"");
}

void MainWindow::SetBusy(bool busy) {
    state_.busy = busy;
    UpdateControls();
}

void MainWindow::UpdateControls() {
    const bool hasInput = !state_.inputFile.empty();
    const bool hasSegments = !state_.segmentEditor.Empty();
    const bool hasSelection = SelectedIntervalIndex() >= 0;
    const bool canEdit = !state_.busy;
    const int selected = SelectedIntervalIndex();

    EnableWindow(browseInputButton_, canEdit);
    EnableWindow(browseOutputButton_, canEdit && hasInput);
    EnableWindow(manualNameEdit_, canEdit && hasInput);
    EnableWindow(startEdit_, canEdit && hasInput);
    EnableWindow(endEdit_, canEdit && hasInput);
    EnableWindow(addButton_, canEdit && hasInput);
    EnableWindow(removeButton_, canEdit && hasSelection);
    EnableWindow(clearButton_, canEdit && hasSegments);
    EnableWindow(moveUpButton_, canEdit && hasSelection && selected > 0);
    EnableWindow(moveDownButton_, canEdit && hasSelection && selected + 1 < static_cast<int>(state_.segmentEditor.Size()));
    EnableWindow(exportButton_, canEdit && hasInput && hasSegments);
    EnableWindow(dryRunButton_, canEdit && hasInput && hasSegments);
    EnableWindow(cancelButton_, state_.busy);

    std::error_code ec;
    const bool canOpenOutput = !state_.busy
        && !state_.lastOutputFile.empty()
        && std::filesystem::exists(state_.lastOutputFile.parent_path(), ec)
        && !ec;
    EnableWindow(openOutputButton_, canOpenOutput);
}

void MainWindow::SetStatus(const std::wstring& status) {
    SetText(statusText_, status);
}

std::wstring MainWindow::GetText(HWND control) const {
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    if (length > 0) {
        GetWindowTextW(control, text.data(), length + 1);
    }
    text.resize(static_cast<size_t>(length));
    return text;
}

void MainWindow::SetText(HWND control, const std::wstring& text) const {
    if (control != nullptr) {
        SetWindowTextW(control, text.c_str());
    }
}

void MainWindow::ShowError(const std::wstring& message) const {
    MessageBoxW(window_, message.c_str(), L"Video Cutter GUI", MB_ICONERROR | MB_OK);
}

} // namespace video_cutter_gui
