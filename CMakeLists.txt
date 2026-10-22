cmake_minimum_required(VERSION 3.20)
project(DiscordHDRHelper VERSION 1.1.8 LANGUAGES CXX RC)

if(NOT MSVC)
    message(FATAL_ERROR "Discord HDR Helper uses C++/WinRT and needs the MSVC toolchain (Visual Studio 2022 or newer).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT so the .exe runs without the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS UNICODE _UNICODE)

add_executable(DiscordHDRHelper WIN32
    src/main.cpp
    src/App.cpp
    src/AudioPassthrough.cpp
    src/Capture.cpp
    src/Display.cpp
    src/Gpu.cpp
    src/OutputWindow.cpp
    src/Panel.cpp
    src/Settings.cpp
    src/Tonemapper.cpp
    src/app.rc
    src/app.manifest)

target_compile_definitions(DiscordHDRHelper PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00
    APP_VERSION="${PROJECT_VERSION}"
    APP_VERSION_NUMBERS=${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0)
target_compile_options(DiscordHDRHelper PRIVATE /W4 /permissive- /utf-8 /EHsc /bigobj)
target_precompile_headers(DiscordHDRHelper PRIVATE src/pch.h)
target_link_libraries(DiscordHDRHelper PRIVATE
    imgui d3d11 dxgi d3dcompiler d2d1 dcomp dwrite dwmapi windowscodecs mmdevapi windowsapp shell32 user32 ole32)
