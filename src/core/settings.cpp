#include "core/settings.h"

namespace vela {

namespace {

bool ReadDword(HKEY key, const wchar_t* name, DWORD& value) {
    DWORD size = sizeof(value);
    return RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS;
}

bool WriteDword(HKEY key, const wchar_t* name, DWORD value) {
    return RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
}

}  // namespace

const wchar_t* TrayIconName(TrayIcon icon) {
    switch (icon) {
        case TrayIcon::Bars: return L"CPU and memory bars";
        case TrayIcon::CpuPercent: return L"CPU %";
        case TrayIcon::MemoryPercent: return L"Memory %";
    }
    return L"";
}

Settings Settings::Load(const wchar_t* keyPath) {
    Settings settings;
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_READ, &key) != ERROR_SUCCESS) return settings;

    DWORD value = 0;
    if (ReadDword(key, L"TrayIcon", value) && value <= static_cast<DWORD>(TrayIcon::MemoryPercent)) {
        settings.trayIcon = static_cast<TrayIcon>(value);
    }
    if (ReadDword(key, L"Interval", value)) {
        for (DWORD interval : kIntervals) {
            if (value == interval) settings.intervalSeconds = value;
        }
    }
    RegCloseKey(key);
    return settings;
}

bool Settings::Save(const wchar_t* keyPath) const {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const bool saved = WriteDword(key, L"TrayIcon", static_cast<DWORD>(trayIcon)) && WriteDword(key, L"Interval", intervalSeconds);
    RegCloseKey(key);
    return saved;
}

bool ConsumeFirstRun(const wchar_t* keyPath) {
    DWORD shown = 0;
    DWORD size = sizeof(shown);
    if (RegGetValueW(HKEY_CURRENT_USER, keyPath, L"FirstRunDone", RRF_RT_REG_DWORD, nullptr, &shown, &size) == ERROR_SUCCESS &&
        shown) {
        return false;
    }
    shown = 1;
    RegSetKeyValueW(HKEY_CURRENT_USER, keyPath, L"FirstRunDone", REG_DWORD, &shown, sizeof(shown));
    return true;
}

std::wstring AutostartCommand(const std::wstring& exePath) { return L"\"" + exePath + L"\" --background"; }

bool IsAutostartEnabled(const wchar_t* valueName) {
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, valueName, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    // Task Manager's "Disable" keeps the Run value and flags it here instead:
    // an odd first byte means disabled.
    BYTE state[12]{};
    DWORD size = sizeof(state);
    if (RegGetValueW(HKEY_CURRENT_USER, kStartupApprovedKey, valueName, RRF_RT_REG_BINARY, nullptr, state, &size) ==
            ERROR_SUCCESS &&
        size > 0) {
        return (state[0] & 1) == 0;
    }
    return true;
}

bool SetAutostartEnabled(bool enabled, const std::wstring& exePath, const wchar_t* valueName) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    LSTATUS status;
    if (enabled) {
        // Setting one named value can never create a duplicate entry.
        const std::wstring command = AutostartCommand(exePath);
        status = RegSetValueExW(key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, valueName);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    // Drop any Task Manager override so the Run value alone decides.
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kStartupApprovedKey, valueName);
    return status == ERROR_SUCCESS;
}

}  // namespace vela
