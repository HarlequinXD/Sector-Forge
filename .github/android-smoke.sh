#!/bin/sh
# Emulator smoke test (Pixel 6 profile: 1080x2400, 420 dpi = 2.625 px per dp, close to a Galaxy S23).
# Portrait is 411x914 dp, landscape 914x411 dp. Taps are dp * 2.625.
set -x
OUT=shots
mkdir -p $OUT
adb shell wm size
adb shell wm density
adb install -r SectorForge-android.apk
adb logcat -c
adb shell am start -n com.harlequinxd.sectorforge/.MainActivity
sleep 12
adb exec-out screencap -p > $OUT/01_home.png
adb shell input tap 858 654                 # dismiss Android's one-time "Viewing full screen" tip
sleep 2
adb shell input tap 675 446                 # "Starter Room" template card
sleep 3
adb exec-out screencap -p > $OUT/02_name_dialog.png
adb shell input text "Smoke%sTest"
adb shell input keyevent 66
sleep 10
adb exec-out screencap -p > $OUT/03_guide.png
adb shell input keyevent 4                  # close the first-run guide (back)
sleep 2
adb exec-out screencap -p > $OUT/04_editor_2d.png
adb shell input tap 215 302                 # Draw tool (rail sits right of the camera cutout)
sleep 1
adb shell input tap 1575 394
adb shell input tap 1838 394
adb shell input tap 1838 656
sleep 1
adb exec-out screencap -p > $OUT/05_drawing.png
adb shell input tap 1575 656
sleep 1
adb shell input tap 1575 656                # double-tap closes the shape
sleep 2
adb exec-out screencap -p > $OUT/06_sector.png
adb shell input tap 1200 68                 # 3D
sleep 3
adb shell input tap 1200 761                # pick the floor
sleep 2
adb shell input swipe 2281 640 2281 520 600 # raise it with the height dial
sleep 2
adb exec-out screencap -p > $OUT/07_3d.png
adb shell input tap 1462 68                 # Play
sleep 4
adb exec-out screencap -p > $OUT/08_play.png
adb shell input keyevent 4                  # back = pause
sleep 2
adb exec-out screencap -p > $OUT/09_pause.png
adb shell input tap 1200 739                # Back to the editor
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
grep -E "SectorForge|AndroidRuntime|FATAL|F DEBUG|F/DEBUG|Fatal signal|relaunch" $OUT/logcat_full.txt > $OUT/logcat.txt || true
exit 0
