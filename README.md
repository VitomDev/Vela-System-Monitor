<p align="center"><img src="assets/vela.png" width="96" alt="Vela System Monitor"></p>

# 📊 Vela — System Monitor
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

Keep an eye on your PC without opening Task Manager. 👀  
CPU and memory live in the tray icon, a tooltip sums up everything, and one click opens a small panel with CPU, memory, disk, network and battery. Want the numbers always in sight? Turn on the floating widget or show them right on the taskbar.

Vela lives in the notification area and is built to stay out of the way: a monitor that eats your resources would defeat its own purpose. 🪶

---

## 🚀 Features

- 📈 **Tray icon**: the Vela logo, or the live CPU or memory percent as a number
- 💬 **Tooltip**: hover the icon for CPU, RAM, disk, network speed and battery at a glance
- 🪟 **Panel**: click the icon for a Windows 11–style flyout with every metric and its level bar
- 📌 **Floating widget**: a tiny always-on-top box with CPU, RAM and network speed. Drag it anywhere, it remembers where
- 🧷 **On the taskbar**: the same numbers right next to the clock, blended into your taskbar
- 🧠 **CPU**: total usage, logical processors and uptime
- 🧮 **Memory**: used and total RAM
- 💽 **Disk**: space used on the system drive plus live read and write speed
- 🌐 **Network**: download and upload speed of your physical adapters (Wi-Fi, Ethernet)
- 🔋 **Battery**: charge, charging state and time left (only on devices with a battery)
- 🚨 Values at 90% or more turn red, in the panel and in the tray icon
- 😴 Stops sampling while the screen is off
- 🔒 100% offline: nothing leaves your PC. No telemetry, no accounts
- 🌙 Clean Windows 11 look that follows your light or dark theme and accent color
- 🪶 Tiny: ~1.3 MB of memory and ~0.03% CPU while running, a ~2.1 MB installer, no .NET or Visual C++ runtime needed
- 🚦 Starts with Windows (you can turn it off)

---

## 📦 Install

Download `VelaSetup.exe` from [Releases](https://github.com/VitomDev/Vela-System-Monitor/releases/latest) and run it.

> 🧰 **Recommended:** install it with [Vitool Box](https://github.com/VitomDev/Vitool-Box), which keeps all VitomDev apps in one place and installs or removes them with a switch.

- Per-user install, no admin rights needed
- Vela starts right away and its icon appears in the notification area
- It starts with Windows from then on

> 💡 Windows may put new icons in the hidden icons area (`^`). Drag Vela's icon onto the taskbar to always see it.

Uninstalling (Settings → Apps) removes Vela, its startup entry and its settings.

---

## 🖱️ Usage

| Action | How |
| --- | --- |
| Quick look | Hover the tray icon |
| Open the panel | Left-click the tray icon |
| Close the panel | Click anywhere else, or `Esc` |
| Floating widget | Right-click the tray icon → **Floating widget**. Drag to move, double-click for the panel, right-click for the menu |
| Numbers on the taskbar | Right-click the tray icon → **Show on taskbar**. Click them for the panel, right-click for the menu |
| Settings | ⚙️ in the panel, or right-click the tray icon → **Settings** |
| Start with Windows | Right-click the tray icon → **Start with Windows** |
| Exit | Right-click the tray icon → **Exit Vela** |

### ⚙️ Settings

- **Tray icon shows**: Vela logo, CPU % or memory %
- **Floating widget** above other windows, on or off
- **Show on the taskbar**, on or off
- **Update**: every 1, 2 (default) or 5 seconds. The panel always updates every second while it is open
- **Start with Windows**

---

## ⚡ Performance

Vela reads the system counters every 2 seconds and only redraws the tray icon or a panel row when what you would see actually changes. The panel only exists while it is open.

Measured on the development PC (Windows 11, default settings, panel closed, over 60 s):

| | |
| --- | --- |
| Tray only | 16 ms CPU (~0.03%), 1.3–1.8 MB working set |
| With floating widget and taskbar view | 31 ms CPU (~0.05%), 2.5 MB working set |
| `Vela.exe` / installer | 0.24 MB / 2.1 MB |

---

## 🔧 Build

Requirements: Visual Studio 2022 or newer with the C++ x64 tools and Inno Setup 6 (`winget install JRSoftware.InnoSetup`).

```powershell
.\build.ps1
```

It downloads the pinned build tools into `.deps/` (Windows SDK, CMake and Ninja, all verified by hash), builds everything, runs the tests and writes the installer to `dist/VelaSetup.exe`.

`.\build.ps1 -SkipTests` and `.\build.ps1 -SkipInstaller` skip those steps.

---

## 📚 Dependencies

None: Vela only uses Windows' own APIs (`GetSystemTimes`, `GlobalMemoryStatusEx`, `GetIfEntry2`, `GetSystemPowerStatus`, disk performance counters). 🧘

---

## 🪟 Compatibility

Windows 10 (version 1809 or newer) and Windows 11, 64-bit.

---

## 🧪 Project Status

✅ Stable.  
This project is fully functional as-is and ready to use.
Future updates to improve features or the interface are not ruled out.

---

## 📄 License

[MIT](LICENSE) © VitomDev

---

## 📲 Contact

Feel free to reach out or follow me:

- 🐙 GitHub: [@VitomDev](https://github.com/VitomDev)
- 🐦 Twitter/X: [@VitomDev](https://twitter.com/VitomDev)
- 🎮 Twitch: [@VitomDev](https://www.twitch.tv/VitomDev)
- 💼 LinkedIn: [VitomDev](https://www.linkedin.com/in/víctor-escós-batalla/)
