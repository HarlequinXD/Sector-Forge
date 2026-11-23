cmake_minimum_required(VERSION 3.16)
project(SectorForge VERSION 0.1.0 LANGUAGES C RC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# standard dynamic C runtime (vcruntime140.dll ships next to the exe)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL$<$<CONFIG:Debug>:Debug>")

set(SOURCES
    src/base.c src/map.c src/mapio.c src/texgen.c src/icons.c src/gfx.c src/ui.c
    src/assets.c src/project.c src/render3d.c src/ed_core.c src/ed2d.c src/ed3d.c
    src/play.c src/panels.c src/app.c src/win32.c
    res/sectorforge.rc)

add_executable(SectorForge WIN32 ${SOURCES})
target_include_directories(SectorForge PRIVATE src)
target_compile_definitions(SectorForge PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(SectorForge PRIVATE opengl32 glu32 gdi32 user32 shell32 ole32 comdlg32 windowscodecs uuid dwmapi)

if(MSVC)
    target_compile_options(SectorForge PRIVATE /W3 /utf-8 /wd4244 /wd4305 /wd4018 /guard:cf)
    target_link_options(SectorForge PRIVATE /guard:cf /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /MANIFEST:EMBED /MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/res/sectorforge.manifest)
else()
    target_compile_options(SectorForge PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
    target_link_options(SectorForge PRIVATE -municode)
endif()
