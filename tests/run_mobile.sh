#!/bin/sh
# Builds and runs the Android touch shell headlessly on Linux against Mesa's OpenGL ES (llvmpipe).
# Needs Khronos GLES3/EGL headers: set SF_GL_INC (default: ../deps/gl/api and ../deps/egl/api).
set -e
cd "$(dirname "$0")/.."
GLINC=${SF_GL_INC:-"-I../deps/gl/api -I../deps/egl/api"}
SRC="src/base.c src/map.c src/mapio.c src/texgen.c src/icons.c src/gfx.c src/ui.c src/assets.c src/project.c src/render3d.c src/tess.c src/ed_core.c src/ed2d.c src/ed3d.c src/play.c src/gles_compat.c src/plat_posix.c src/font_stb.c"
gcc -std=c11 -g -O1 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-format-truncation -fsanitize=address,undefined \
    -DSF_GLES $GLINC tests/mobile_harness.c $SRC ${SF_GL_LIBS:--lEGL -lGLESv2} -lm -o /tmp/sf_mobile
ASAN_OPTIONS=detect_leaks=0 /tmp/sf_mobile
