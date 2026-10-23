#include "app/tray_app.h"

#include <commctrl.h>

#include <cwchar>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    // "--background" is used by the Start with Windows entry: start silently.
    const bool startedAtSignIn = wcsstr(commandLine, L"--background") != nullptr;

    // One instance per user session. A second launch only opens the panel of
    // the running one.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\VelaSystemMonitor.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND running = FindWindowW(vela::kTrayWindowClass, nullptr); running && !startedAtSignIn) {
            // Let the running instance take the foreground for its panel.
            DWORD process = 0;
            GetWindowThreadProcessId(running, &process);
            AllowSetForegroundWindow(process);
            PostMessageW(running, vela::WM_APP_ALREADY_RUNNING, 0, 0);
        }
        return 0;
    }

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    int exitCode = 0;
    {
        vela::TrayApp app;
        if (app.Initialize(instance, startedAtSignIn)) {
            MSG message;
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        } else {
            exitCode = 1;
        }
    }
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return exitCode;
}
