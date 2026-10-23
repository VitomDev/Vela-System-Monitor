// Vela's automated tests: a minimal runner without dependencies.
//
//   VelaTests.exe <build dir>
//
// The build dir holds Vela.exe, used by the lifecycle test. Tests that need
// something unavailable report SKIP instead of passing.

#include "app/panel.h"
#include "app/tray_icon.h"
#include "core/metrics.h"
#include "core/sampler.h"
#include "core/settings.h"

#include <windows.h>

#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace vela;
namespace fs = std::filesystem;

namespace {

struct TestCase {
    const char* name;
    std::function<void()> run;
};

std::vector<TestCase>& Tests() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> run) { Tests().push_back({name, std::move(run)}); }
};

struct Failure {
    std::string message;
};
struct Skip {
    std::string reason;
};

#define TEST(name)                                     \
    static void name();                                \
    static Registrar name##_registrar(#name, name);    \
    static void name()

#define CHECK(expression)                                                                                  \
    do {                                                                                                   \
        if (!(expression)) throw Failure{std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + #expression}; \
    } while (false)

#define SKIP(reason) throw Skip{reason}

fs::path buildDir;

constexpr wchar_t kTestSettingsKey[] = L"Software\\Vela System Monitor Tests";
constexpr wchar_t kTestRunValue[] = L"Vela System Monitor Test";

Snapshot SampleSnapshot() {
    Snapshot s;
    s.cpuPercent = 23.4;
    s.memoryUsed = 6ull << 30;
    s.memoryTotal = 16ull << 30;
    s.networkDown = 1536;
    s.networkUp = 0;
    s.diskLetter = L'D';
    s.diskUsed = 300ull << 30;
    s.diskTotal = 400ull << 30;
    return s;
}

}  // namespace

// --- Metrics ------------------------------------------------------------------------------------

TEST(CpuPercentIsTheBusyShare) {
    // Kernel time includes idle time, as GetSystemTimes reports it.
    CHECK(CpuPercent({100, 200, 100}, {175, 300, 200}) == 62.5);
    CHECK(CpuPercent({0, 0, 0}, {100, 100, 0}) == 0);
    CHECK(CpuPercent({0, 0, 0}, {0, 50, 50}) == 100);
}

TEST(CpuPercentIgnoresMissingOrBackwardCounters) {
    CHECK(CpuPercent({10, 10, 10}, {10, 10, 10}) == 0);
    CHECK(CpuPercent({10, 50, 50}, {5, 60, 60}) == 0);
}

TEST(RatesArePerSecond) {
    CHECK(RatePerSecond(1000, 3000, 2000) == 1000);
    CHECK(RatePerSecond(0, 500, 500) == 1000);
    // A counter reset, or no time elapsed, reports no traffic.
    CHECK(RatePerSecond(5000, 100, 1000) == 0);
    CHECK(RatePerSecond(0, 100, 0) == 0);
}

TEST(FormatsBytesRatesAndDurations) {
    CHECK(FormatBytes(0) == L"0 B");
    CHECK(FormatBytes(999) == L"999 B");
    CHECK(FormatBytes(1536) == L"1.5 KB");
    CHECK(FormatBytes(16ull << 30) == L"16.0 GB");
    CHECK(FormatBytes(476ull << 30) == L"476 GB");
    CHECK(FormatBytes(2ull << 40) == L"2.0 TB");
    CHECK(FormatRate(1258291) == L"1.2 MB/s");
    CHECK(FormatDuration(45 * 60) == L"45 min");
    CHECK(FormatDuration(2 * 3600) == L"2 h");
    CHECK(FormatDuration(2 * 3600 + 5 * 60) == L"2 h 5 min");
    CHECK(FormatDuration(152 * 3600 + 4 * 60) == L"6 d 8 h");
    CHECK(FormatDuration(-1).empty());
    CHECK(FormatPercent(23.6) == L"24%");
}

TEST(ReadsTheBatteryFromThePowerStatus) {
    SYSTEM_POWER_STATUS status{};
    status.BatteryFlag = 128;  // No system battery
    status.BatteryLifePercent = 255;
    CHECK(!BatteryFromPowerStatus(status).present);

    status = {};
    status.ACLineStatus = 0;
    status.BatteryFlag = 1;
    status.BatteryLifePercent = 64;
    status.BatteryLifeTime = 5400;
    BatteryInfo battery = BatteryFromPowerStatus(status);
    CHECK(battery.present && battery.percent == 64 && !battery.pluggedIn && !battery.charging && battery.secondsLeft == 5400);

    status.ACLineStatus = 1;
    status.BatteryFlag = 8;
    status.BatteryLifeTime = static_cast<DWORD>(-1);
    battery = BatteryFromPowerStatus(status);
    CHECK(battery.pluggedIn && battery.charging && battery.secondsLeft == -1);

    status.BatteryFlag = 255;  // Unknown
    CHECK(!BatteryFromPowerStatus(status).present);
}

TEST(TooltipSummarizesAndFitsTheLimit) {
    Snapshot s = SampleSnapshot();
    std::wstring text = TooltipText(s);
    CHECK(text == L"CPU 23% \u00B7 RAM 38%\nDisk D: 75%\n\u2193 1.5 KB/s  \u2191 0 B/s");

    s.battery = {true, 80, false, false, 3900};
    text = TooltipText(s);
    CHECK(text.ends_with(L"\nBattery 80%, 1 h 5 min left"));
    s.battery.pluggedIn = s.battery.charging = true;
    CHECK(TooltipText(s).ends_with(L"\nBattery 80%, charging"));

    s.networkDown = s.networkUp = UINT64_MAX;
    CHECK(TooltipText(s).size() <= 127);
}

TEST(SamplerReadsPlausibleValues) {
    Sampler sampler;
    const Snapshot first = sampler.Sample();
    CHECK(first.cpuPercent == 0 && first.networkDown == 0 && first.diskRead == 0);
    CHECK(first.memoryTotal > 0 && first.memoryUsed <= first.memoryTotal);
    CHECK(first.diskTotal > 0 && first.diskUsed <= first.diskTotal);
    Sleep(300);
    const Snapshot second = sampler.Sample();
    CHECK(second.cpuPercent >= 0 && second.cpuPercent <= 100);
    CHECK(second.MemoryPercent() > 0 && second.MemoryPercent() <= 100);
    wchar_t windows[MAX_PATH];
    GetSystemWindowsDirectoryW(windows, MAX_PATH);
    CHECK(second.diskLetter == windows[0]);
}

// --- Panel and tray icon ------------------------------------------------------------------------

TEST(PanelRowsDescribeTheSnapshot) {
    Snapshot s = SampleSnapshot();
    std::vector<PanelRow> rows = BuildPanelRows(s, 8, 3 * 3600 * 1000ull);
    CHECK(rows.size() == 4);
    CHECK(rows[0].label == L"CPU" && rows[0].value == L"23%" && rows[0].barPercent == 23 && !rows[0].high);
    CHECK(rows[0].detail == L"8 logical processors \u00B7 up 3 h");
    CHECK(rows[1].value == L"38%" && rows[1].detail == L"6.0 GB of 16.0 GB");
    CHECK(rows[2].label == L"Disk (D:)" && rows[2].detail == L"300 GB of 400 GB");
    CHECK(rows[3].barPercent == -1 && rows[3].detail == L"\u2193 1.5 KB/s   \u2191 0 B/s");

    s.cpuPercent = 97;
    s.diskRatesKnown = true;
    s.diskWrite = 2048;
    s.battery = {true, 8, false, false, -1};
    rows = BuildPanelRows(s, 8, 0);
    CHECK(rows.size() == 5);
    CHECK(rows[0].high);
    CHECK(rows[2].detail == L"300 GB of 400 GB \u00B7 R 0 B/s \u00B7 W 2.0 KB/s");
    CHECK(rows[4].label == L"Battery" && rows[4].detail == L"On battery" && rows[4].high);
}

TEST(TrayIconOnlyChangesWhenVisible) {
    // A 16 px icon has 10 px bars: small changes look the same.
    const TrayIconState a = MakeTrayIconState(TrayIcon::Bars, 16, false, 41.0, 50);
    const TrayIconState b = MakeTrayIconState(TrayIcon::Bars, 16, false, 44.0, 52);
    const TrayIconState c = MakeTrayIconState(TrayIcon::Bars, 16, false, 80.0, 50);
    CHECK(a == b);
    CHECK(!(a == c));
    // Idle still shows a sliver; overload turns the warning color on.
    CHECK(MakeTrayIconState(TrayIcon::Bars, 16, false, 0, 0).cpu == 1);
    CHECK(MakeTrayIconState(TrayIcon::Bars, 16, false, 95, 0).cpuHigh);
    // Numbers change with every percent, and ignore the other metric.
    const TrayIconState cpu = MakeTrayIconState(TrayIcon::CpuPercent, 16, false, 41.0, 50);
    CHECK(cpu.cpu == 41 && cpu.memory == 0);
    CHECK(MakeTrayIconState(TrayIcon::CpuPercent, 16, false, 41.0, 99) == cpu);
    CHECK(!(MakeTrayIconState(TrayIcon::CpuPercent, 16, true, 41.0, 50) == cpu));
}

TEST(TrayIconsAreCreatedAtTheRequestedSize) {
    for (TrayIcon style : kTrayIcons) {
        for (int size : {16, 20, 24, 32}) {
            HICON icon = CreateTrayIcon(MakeTrayIconState(style, size, false, 100, 100));
            CHECK(icon != nullptr);
            ICONINFO info{};
            CHECK(GetIconInfo(icon, &info));
            BITMAP bitmap{};
            GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap);
            CHECK(bitmap.bmWidth == size && bitmap.bmHeight == size);
            DeleteObject(info.hbmColor);
            DeleteObject(info.hbmMask);
            DestroyIcon(icon);
        }
    }
}

// --- Settings and Start with Windows ---------------------------------------------------

TEST(SettingsPersist) {
    RegDeleteTreeW(HKEY_CURRENT_USER, kTestSettingsKey);
    Settings settings = Settings::Load(kTestSettingsKey);
    CHECK(settings.trayIcon == TrayIcon::Bars);
    CHECK(settings.intervalSeconds == 2);

    settings.trayIcon = TrayIcon::MemoryPercent;
    settings.intervalSeconds = 5;
    CHECK(settings.Save(kTestSettingsKey));
    const Settings loaded = Settings::Load(kTestSettingsKey);
    CHECK(loaded.trayIcon == TrayIcon::MemoryPercent && loaded.intervalSeconds == 5);
    RegDeleteTreeW(HKEY_CURRENT_USER, kTestSettingsKey);
}

TEST(InvalidStoredSettingsFallBackToDefaults) {
    HKEY key;
    CHECK(RegCreateKeyExW(HKEY_CURRENT_USER, kTestSettingsKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS);
    const DWORD badIcon = 7, badInterval = 0;
    RegSetValueExW(key, L"TrayIcon", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&badIcon), sizeof(badIcon));
    RegSetValueExW(key, L"Interval", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&badInterval), sizeof(badInterval));
    RegCloseKey(key);
    const Settings loaded = Settings::Load(kTestSettingsKey);
    CHECK(loaded.trayIcon == TrayIcon::Bars);
    CHECK(loaded.intervalSeconds == 2);
    RegDeleteTreeW(HKEY_CURRENT_USER, kTestSettingsKey);
}

TEST(FirstRunIsReportedOnce) {
    RegDeleteTreeW(HKEY_CURRENT_USER, kTestSettingsKey);
    CHECK(ConsumeFirstRun(kTestSettingsKey));
    CHECK(!ConsumeFirstRun(kTestSettingsKey));
    CHECK(Settings{}.Save(kTestSettingsKey));
    CHECK(!ConsumeFirstRun(kTestSettingsKey));
    RegDeleteTreeW(HKEY_CURRENT_USER, kTestSettingsKey);
}

TEST(AutostartCanBeEnabledDisabledAndReenabled) {
    const std::wstring exe = L"C:\\Program Files\\Vela Test\\Vela.exe";
    SetAutostartEnabled(false, exe, kTestRunValue);
    CHECK(!IsAutostartEnabled(kTestRunValue));
    CHECK(SetAutostartEnabled(true, exe, kTestRunValue));
    CHECK(IsAutostartEnabled(kTestRunValue));
    CHECK(SetAutostartEnabled(true, exe, kTestRunValue));
    wchar_t value[512]{};
    DWORD size = sizeof(value);
    CHECK(RegGetValueW(HKEY_CURRENT_USER, kRunKey, kTestRunValue, RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS);
    CHECK(std::wstring(value) == L"\"C:\\Program Files\\Vela Test\\Vela.exe\" --background");
    CHECK(SetAutostartEnabled(false, exe, kTestRunValue));
    CHECK(!IsAutostartEnabled(kTestRunValue));
}

TEST(AutostartRespectsTaskManagerDisable) {
    const std::wstring exe = L"C:\\Vela\\Vela.exe";
    CHECK(SetAutostartEnabled(true, exe, kTestRunValue));
    // Task Manager marks a disabled startup entry with an odd first byte.
    const BYTE disabled[12]{3};
    RegSetKeyValueW(HKEY_CURRENT_USER, kStartupApprovedKey, kTestRunValue, REG_BINARY, disabled, sizeof(disabled));
    CHECK(!IsAutostartEnabled(kTestRunValue));
    CHECK(SetAutostartEnabled(true, exe, kTestRunValue));
    CHECK(IsAutostartEnabled(kTestRunValue));
    CHECK(SetAutostartEnabled(false, exe, kTestRunValue));
}

// --- Application lifecycle ----------------------------------------------------------------------

namespace {

HWND FindVelaWindow() { return FindWindowW(L"VelaSystemMonitorTray", nullptr); }

PROCESS_INFORMATION StartVela() {
    std::wstring command = L"\"" + (buildDir / L"Vela.exe").wstring() + L"\" --background";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process);
    if (process.hThread) CloseHandle(process.hThread);
    return process;
}

HWND WaitForVelaWindow() {
    for (int i = 0; i < 100; ++i) {
        if (HWND window = FindVelaWindow()) return window;
        Sleep(50);
    }
    return nullptr;
}

}  // namespace

TEST(SingleInstanceAndCleanShutdown) {
    if (!fs::exists(buildDir / L"Vela.exe")) SKIP("Vela.exe not built");
    if (FindVelaWindow()) SKIP("Vela is already running on this machine");

    // Keep the user's first-run state: mark it done for this run, restore it after.
    const bool firstRunPending = ConsumeFirstRun();
    struct RestoreFirstRun {
        bool pending;
        ~RestoreFirstRun() {
            if (pending) RegDeleteKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"FirstRunDone");
        }
    } restoreFirstRun{firstRunPending};

    PROCESS_INFORMATION first = StartVela();
    CHECK(first.hProcess != nullptr);
    HWND window = WaitForVelaWindow();
    CHECK(window != nullptr);

    // A second launch exits at once and leaves the first one running.
    PROCESS_INFORMATION second = StartVela();
    CHECK(second.hProcess != nullptr);
    CHECK(WaitForSingleObject(second.hProcess, 5000) == WAIT_OBJECT_0);
    DWORD secondExit = 1;
    GetExitCodeProcess(second.hProcess, &secondExit);
    CloseHandle(second.hProcess);
    CHECK(secondExit == 0);
    CHECK(WaitForSingleObject(first.hProcess, 0) == WAIT_TIMEOUT);

    // WM_CLOSE is what the installer sends; Vela must exit cleanly.
    PostMessageW(window, WM_CLOSE, 0, 0);
    CHECK(WaitForSingleObject(first.hProcess, 5000) == WAIT_OBJECT_0);
    DWORD firstExit = 1;
    GetExitCodeProcess(first.hProcess, &firstExit);
    CloseHandle(first.hProcess);
    CHECK(firstExit == 0);
    CHECK(FindVelaWindow() == nullptr);
}

// --- Runner -----------------------------------------------------------------------------------------

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    buildDir = argc > 1 ? fs::path(argv[1]) : fs::path(argv[0]).parent_path();

    int passed = 0, failed = 0, skipped = 0;
    for (const auto& test : Tests()) {
        try {
            test.run();
            ++passed;
            std::cout << "PASS " << test.name << "\n";
        } catch (const Skip& skip) {
            ++skipped;
            std::cout << "SKIP " << test.name << ": " << skip.reason << "\n";
        } catch (const Failure& failure) {
            ++failed;
            std::cout << "FAIL " << test.name << ": " << failure.message << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "FAIL " << test.name << ": exception: " << e.what() << "\n";
        }
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed, " << skipped << " skipped\n";
    return failed == 0 ? 0 : 1;
}
