#!/bin/sh
# Emulator smoke test: install the APK, walk through home -> new project -> editor 2D/3D/Play,
# grab screenshots and the app log. Coordinates are for a 1080x2340 screen at 420 dpi.
set -x
OUT=shots
mkdir -p $OUT
adb shell wm size 1080x2340
adb shell wm density 420
sleep 2
adb shell wm size
adb install -r SectorForge-android.apk
adb logcat -c
adb shell am start -n com.harlequinxd.sectorforge/.MainActivity
sleep 12
adb exec-out screencap -p > $OUT/01_home.png
# "Starter Room" template card
adb shell input tap 675 420
sleep 3
adb exec-out screencap -p > $OUT/02_name_dialog.png
adb shell input text "Smoke%sTest"
adb shell input keyevent 66
sleep 10
adb exec-out screencap -p > $OUT/03_editor.png
# close the first-run guide (top-left X)
adb shell input tap 100 85
sleep 2
adb exec-out screencap -p > $OUT/04_editor_2d.png
# Draw tool, then three taps and Close shape via double-tap
adb shell input tap 135 330
sleep 1
adb shell input tap 1500 380
adb shell input tap 1800 380
adb shell input tap 1800 650
sleep 1
adb exec-out screencap -p > $OUT/05_drawing.png
# 3D
adb shell input tap 1169 68
sleep 3
adb exec-out screencap -p > $OUT/06_3d.png
# Play
adb shell input tap 1431 68
sleep 4
adb exec-out screencap -p > $OUT/07_play.png
adb shell input keyevent 4
sleep 2
adb exec-out screencap -p > $OUT/08_pause.png
adb shell pidof com.harlequinxd.sectorforge > $OUT/pid.txt || echo "NOT RUNNING" > $OUT/pid.txt
adb logcat -d -v brief > $OUT/logcat_full.txt
grep -E "SectorForge|AndroidRuntime|FATAL|DEBUG|libc|sfgl|Abort" $OUT/logcat_full.txt > $OUT/logcat.txt || true
exit 0
