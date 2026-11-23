#!/bin/sh
# Builds and runs the headless harness on Linux with AddressSanitizer + UBSan.
set -e
cd "$(dirname "$0")/.."
SRC="src/base.c src/map.c src/mapio.c src/texgen.c src/icons.c src/gfx.c src/ui.c src/assets.c src/project.c src/render3d.c src/ed_core.c src/ed2d.c src/ed3d.c src/play.c src/panels.c src/app.c"
gcc -std=c11 -g -O1 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -fsanitize=address,undefined \
    -Itests/stub tests/harness.c tests/stub_gl.c $SRC -lm -o /tmp/sf_harness
ASAN_OPTIONS=detect_leaks=0 /tmp/sf_harness
