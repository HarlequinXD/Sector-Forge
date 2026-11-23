#!/bin/sh
# Emulator smoke test: install the APK, walk through home -> new project -> editor 2D/3D/Play,
# grab screenshots and the app log. The default emulator screen is 1080x1920 at 420 dpi
# (2.625 px per dp): portrait 411x731 dp, landscape 731x411 dp. Taps below are dp * 2.625.
set -x
OUT=shots
mkdir -p $OUT
adb shell wm size
adb install -r SectorForge-android.apk
adb logcat -c
adb shell am start -n com.harlequinxd.sectorforge/.MainActivity
sleep 12
adb exec-out screencap -p > $OUT/01_home.png
adb shell input tap 675 420                 # "Starter Room" template card
sleep 3
adb exec-out screencap -p > $OUT/02_name_dialog.png
adb shell input text "Smoke%sTest"
adb shell input keyevent 66
sleep 10
adb exec-out screencap -p > $OUT/03_guide.png
adb shell input tap 100 85                  # close the first-run guide
sleep 2
adb exec-out screencap -p > $OUT/04_editor_2d.png
adb shell input tap 89 302                  # Draw tool
sleep 1
adb shell input tap 1300 420
adb shell input tap 1560 420
adb shell input tap 1560 680
sleep 1
adb exec-out screencap -p > $OUT/05_drawing.png
adb shell input tap 1300 680
sleep 1
adb shell input tap 1300 680                # double-tap closes the shape
sleep 2
adb exec-out screencap -p > $OUT/06_sector.png
adb shell input tap 959 68                  # 3D
sleep 3
adb shell input tap 1181 787                # pick the floor
sleep 2
adb shell input swipe 1800 640 1800 520 600 # raise it with the height dial
sleep 2
adb exec-out screencap -p > $OUT/07_3d.png
adb shell input tap 1177 68                 # Play
sleep 4
adb exec-out screencap -p > $OUT/08_play.png
adb shell input keyevent 4                  # back = pause
sleep 2
adb exec-out screencap -p > $OUT/09_pause.png
adb shell input tap 959 739                 # Back to the editor
sleep 2
adb exec-out screencap -p > $OUT/10_back_in_editor.png
adb shell input keyevent 4                  # back from 3D -> 2D
sleep 1
adb shell input keyevent 4                  # back from 2D -> home (saves)
sleep 4
adb exec-out screencap -p > $OUT/11_home_again.png
adb shell pidof com.harlequinxd.sectorforge > $OUT/pid.txt || echo "NOT RUNNING" > $OUT/pid.txt
adb shell ls -R /storage/emulated/0/Android/data/com.harlequinxd.sectorforge/files > $OUT/files.txt 2>&1
adb logcat -d -v brief > $OUT/logcat_full.txt
grep -E "SectorForge|AndroidRuntime|FATAL|F DEBUG|F/DEBUG|Fatal signal" $OUT/logcat_full.txt > $OUT/logcat.txt || true
exit 0
