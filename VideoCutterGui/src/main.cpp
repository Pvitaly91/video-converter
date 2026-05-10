#include "MainWindow.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <CommCtrl.h>
#include <Windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int commandShow) {
    HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&controls);

    video_cutter_gui::MainWindow window;
    if (!window.Create(instance, commandShow)) {
        if (SUCCEEDED(comResult)) {
            CoUninitialize();
        }
        return 1;
    }

    const int result = window.RunMessageLoop();
    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }
    return result;
}
