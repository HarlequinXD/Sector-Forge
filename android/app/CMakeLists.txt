cmake_minimum_required(VERSION 3.22.1)
project(sectorforge C)

set(SF ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

add_library(native_app_glue STATIC ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)

add_library(sectorforge SHARED
    ${SF}/android.c
    ${SF}/app_mobile.c
    ${SF}/plat_posix.c
    ${SF}/font_stb.c
    ${SF}/gles_compat.c
    ${SF}/tess.c
    ${SF}/base.c
    ${SF}/map.c
    ${SF}/mapio.c
    ${SF}/texgen.c
    ${SF}/icons.c
    ${SF}/gfx.c
    ${SF}/ui.c
    ${SF}/assets.c
    ${SF}/project.c
    ${SF}/render3d.c
    ${SF}/ed_core.c
    ${SF}/ed2d.c
    ${SF}/ed3d.c
    ${SF}/play.c)

set_target_properties(sectorforge PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_compile_definitions(sectorforge PRIVATE SF_GLES _GNU_SOURCE)
target_compile_options(sectorforge PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare -Wno-format-truncation -ffast-math)
# NativeActivity looks this symbol up at runtime; keep the linker from dropping it.
target_link_options(sectorforge PRIVATE -u ANativeActivity_onCreate -Wl,-z,max-page-size=16384)
target_link_libraries(sectorforge native_app_glue android EGL GLESv3 log m)
