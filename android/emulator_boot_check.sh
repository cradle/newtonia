#!/bin/bash
# Boot check for Android Automotive OS (the car-installed Android, as opposed
# to Android Auto's phone projection): install the APK on an automotive
# emulator, launch the game normally, and fail unless it gets past GL context
# and audio setup and is still the resumed activity a while later. Used by
# .github/workflows/android.yml's automotive-boot job; works unchanged against
# any attached device. Saves a screenshot (automotive.png) of what the car
# screen shows.
#
# The install itself is part of the check: a hardware feature the manifest
# marks required, which cars lack, makes the package manager refuse it.
#
# APK=<path> overrides the default.
set -u
APK="${APK:-app-release.apk}"
PKG=org.newtonia
ACTIVITY=$PKG/.NewtoniaActivity
WAIT_S="${WAIT_S:-90}"     # for the boot log line
SETTLE_S="${SETTLE_S:-20}" # running time after it, before the verdict

[ -f "$APK" ] || { echo "FATAL: no APK at $APK"; exit 1; }
adb wait-for-device

# Make sure this really is a car image, or the check proves nothing.
if ! adb shell pm list features | grep -q "android.hardware.type.automotive"; then
  echo "FATAL: device does not report android.hardware.type.automotive"
  exit 1
fi
echo "Automotive OS $(adb shell getprop ro.build.version.release | tr -d '\r')"

adb install -r -g "$APK" || { echo "FATAL: install failed"; exit 1; }
# Let the install finish landing for every user before launching. The car
# image runs several Android users and finishes the install for them (and
# starts/stops background users) for a few seconds afterwards; a package
# change while the game is up makes Android RELAUNCH its activity, and an
# SDL game ends its main loop on that relaunch, so a launch straight after
# install closed itself about a second later in the first runs.
sleep "${INSTALL_SETTLE_S:-45}"

log=logcat-automotive.txt
: > "$log"
adb logcat -c || true
adb logcat -v time SDL/APP:V AndroidRuntime:E DEBUG:F libc:F '*:S' > "$log" 2>&1 &
logpid=$!
# Everything else (system + event buffers, every tag) goes to the artifact for
# diagnosing lifecycle surprises: which activity took the screen and why.
adb logcat -b main,system,events,crash -v time > logcat-automotive-full.txt 2>&1 &
fullpid=$!
adb shell am start -S -n "$ACTIVITY" > /dev/null

booted=
for _ in $(seq 1 "$WAIT_S"); do
  # Logged after SDL_GL_CreateContext succeeded and the mixer opened; the
  # state machine starts right after.
  if grep -qE "Mix opened|Mix_OpenAudioDevice failed" "$log"; then booted=1; break; fi
  grep -qE "FATAL EXCEPTION|Fatal signal" "$log" && break
  sleep 1
done
[ -n "$booted" ] && sleep "$SETTLE_S"

adb exec-out screencap -p > automotive.png 2>/dev/null || true
resumed=$(adb shell dumpsys activity activities | grep -E "mResumedActivity|topResumedActivity" | tr -d '\r')
pid=$(adb shell pidof "$PKG" | tr -d '\r')
adb shell dumpsys activity activities > dumpsys-activities.txt 2>&1
kill "$logpid" "$fullpid" 2>/dev/null
wait "$logpid" "$fullpid" 2>/dev/null

fail=
[ -n "$booted" ] || { echo "FAIL: never reached audio setup (GL context or earlier)"; fail=1; }
grep -qE "FATAL EXCEPTION|Fatal signal" "$log" && { echo "FAIL: crash in logcat"; fail=1; }
[ -n "$pid" ] || { echo "FAIL: $PKG is not running"; fail=1; }
grep -q "wm_relaunch_resume_activity.*$PKG" logcat-automotive-full.txt && \
  echo "NOTE: Android relaunched the activity during the run (see the events buffer)"
echo "$resumed" | grep -q "$PKG" || { echo "FAIL: $PKG is not the resumed activity:"; echo "$resumed"; fail=1; }

if [ -n "$fail" ]; then
  echo "== logcat:"
  tail -60 "$log"
  echo "== activity lifecycle (events buffer):"
  grep -E "wm_(on_|create|restart|finish|destroy|set_resumed|pause|stop|task_moved)|am_(crash|anr|kill|proc_died)|org\.newtonia" \
    logcat-automotive-full.txt | grep -v "SDL/APP" | tail -60
  echo "== SDL and input around the launch (full logcat):"
  grep -E "SDL|KeyEvent|keycode|KEYCODE|InputDispatcher|ActivityTaskManager|$PKG" \
    logcat-automotive-full.txt | grep -vE "Adding device|PlayerBase|TrackPlayer" | head -150
  echo "== $PKG in dumpsys:"
  grep -nE "Display #|$PKG|ResumedActivity" dumpsys-activities.txt | head -60
  exit 1
fi
grep -E "SDL_CreateWindow|GL|Mix opened|Car display" "$log" | head -10
echo "AUTOMOTIVE BOOT PASS"
