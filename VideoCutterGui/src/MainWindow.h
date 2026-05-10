#pragma once

#include "GuiState.h"
#include "VideoSegmentExporter.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <memory>
#include <string>

namespace video_cutter_gui {

class MainWindow {
public:
    bool Create(HINSTANCE instance, int commandShow);
    int RunMessageLoop();

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void CreateControls();
    HWND CreateControl(const wchar_t* className,
                       const wchar_t* text,
                       DWORD style,
                       DWORD exStyle,
                       int x,
                       int y,
                       int width,
                       int height,
                       int id);
    HWND CreateLabel(const wchar_t* text, int x, int y, int width, int height);
    void ApplyFont(HWND control) const;

    void PickInputVideo();
    void PickOutputFolder();
    void StartProbe();
    void HandleProbeResult(int generation, std::unique_ptr<VideoInfo> info);

    void AddInterval();
    void RemoveSelectedInterval();
    void MoveSelectedInterval(int direction);
    void ClearIntervals();
    void RefreshIntervalList(int selectIndex = -1);
    int SelectedIntervalIndex() const;

    void UpdateOutputPreview();
    void StartExport(bool dryRun);
    void CancelExport();
    void HandleExportDone(std::unique_ptr<ExportResult> result);
    void OpenOutputFolder();

    void AppendLogLine(const std::wstring& text);
    void ClearLog();
    void SetBusy(bool busy);
    void UpdateControls();
    void SetStatus(const std::wstring& status);
    std::wstring GetText(HWND control) const;
    void SetText(HWND control, const std::wstring& text) const;
    void ShowError(const std::wstring& message) const;

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HFONT font_ = nullptr;

    HWND inputEdit_ = nullptr;
    HWND browseInputButton_ = nullptr;
    HWND metadataEdit_ = nullptr;
    HWND startEdit_ = nullptr;
    HWND endEdit_ = nullptr;
    HWND addButton_ = nullptr;
    HWND intervalList_ = nullptr;
    HWND removeButton_ = nullptr;
    HWND clearButton_ = nullptr;
    HWND moveUpButton_ = nullptr;
    HWND moveDownButton_ = nullptr;
    HWND outputFolderEdit_ = nullptr;
    HWND browseOutputButton_ = nullptr;
    HWND manualNameEdit_ = nullptr;
    HWND outputPreviewEdit_ = nullptr;
    HWND exportButton_ = nullptr;
    HWND dryRunButton_ = nullptr;
    HWND cancelButton_ = nullptr;
    HWND openOutputButton_ = nullptr;
    HWND progressBar_ = nullptr;
    HWND statusText_ = nullptr;
    HWND logEdit_ = nullptr;

    GuiState state_;
    std::atomic_int probeGeneration_{0};
    std::shared_ptr<VideoSegmentExporter> activeExporter_;
};

} // namespace video_cutter_gui
