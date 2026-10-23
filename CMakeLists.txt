# Vela.exe, its core library and the tests. Use build.ps1, which also fetches
# the build tools and builds the installer.
cmake_minimum_required(VERSION 3.21)
project(Vela CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: no Visual C++ redistributable needed.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)

add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00)
add_compile_options(/W4 /WX /permissive- /sdl /utf-8 /guard:cf /external:anglebrackets /external:W0)
add_link_options(/guard:cf /CETCOMPAT /OPT:REF /OPT:ICF)

add_library(velacore STATIC
    src/core/metrics.cpp
    src/core/paths.cpp
    src/core/sampler.cpp
    src/core/settings.cpp
)
target_include_directories(velacore PUBLIC src)
target_link_libraries(velacore PUBLIC user32 advapi32 iphlpapi)

add_executable(Vela WIN32
    src/app/main.cpp
    src/app/mini_views.cpp
    src/app/panel.cpp
    src/app/settings_dialog.cpp
    src/app/theme.cpp
    src/app/tray_app.cpp
    src/app/tray_icon.cpp
    src/app/Vela.rc
)
target_link_libraries(Vela PRIVATE velacore gdi32 shell32 comctl32 dwmapi uxtheme)
# The manifest is embedded through Vela.rc.
target_link_options(Vela PRIVATE /MANIFEST:NO)

add_executable(VelaTests tests/tests.cpp src/app/panel.cpp src/app/theme.cpp src/app/tray_icon.cpp)
target_link_libraries(VelaTests PRIVATE velacore gdi32 comctl32 dwmapi uxtheme)
