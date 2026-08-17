#!/bin/bash
# Run the Android relay self-test on an attached device or emulator and turn
# its LOG LINE into an exit status. Used by .github/workflows/android.yml
# inside reactivecircus/android-emulator-runner; works unchanged against a
# real phone (adb devices) if you have one plugged in.
#
# The verdict is the log line, not the exit code: an Android app's exit
# status never reaches adb (android_main.cpp says the same beside the hook).
# So this greps SDL/APP for the same strings the desktop and iOS hooks
# print, and exits non-zero unless it sees the PASS.
#
# APK=<path> overrides the default; SELFTEST picks the hook.
set -u
APK="${APK:-app-debug.apk}"
SELFTEST="${SELFTEST:-NEWTONIA_SIGNAL_SELFTEST}"
ACTIVITY=org.newtonia/.NewtoniaActivity
# The signal hook settles in ~20 s on a phone; an emulator on a shared
# runner is slower, and a FAILING attempt has its own internal timeouts to
# burn through before it says so.
WAIT_S="${WAIT_S:-90}"

[ -f "$APK" ] || { echo "FATAL: no APK at $APK"; exit 1; }
adb wait-for-device
adb install -r -g "$APK" || { echo "FATAL: install failed"; exit 1; }

ok=
for attempt in 1 2 3; do
  log="logcat-$attempt.txt"
  : > "$log"
  adb logcat -c || true
  # -S: restart the activity so the extras are re-delivered rather than the
  # existing task being resumed. applyEnvExtras Os.setenvs them before the
  # native start, which is the only reason SDL_getenv can see them.
  adb shell am start -S -n "$ACTIVITY" --es "$SELFTEST" 1 > /dev/null
  # Streamed to a file rather than piped into grep: a streaming logcat never
  # returns, and a pipeline that dies on the first match takes its exit
  # status with it.
  adb logcat -s SDL/APP > "$log" 2>&1 &
  logpid=$!
  for _ in $(seq 1 "$WAIT_S"); do
    grep -qE "SELFTEST (PASS|FAIL)" "$log" && break
    sleep 1
  done
  kill "$logpid" 2>/dev/null
  wait "$logpid" 2>/dev/null

  if grep -q "SELFTEST PASS" "$log"; then
    ok=1
    break
  fi
  echo "== attempt $attempt did not pass; SDL/APP said:"
  grep -iE "selftest|net: tls|net_signal" "$log" | tail -20 || true
  # Same reasoning as the desktop gates: the relay's limiter is a 10-minute
  # FIXED window that refused requests also feed, so retrying digs the next
  # job on this IP deeper in rather than clearing it.
  if grep -q "rate-limited" "$log"; then
    echo "the relay rate-limited this runner - not retrying (10-minute fixed window)"
    break
  fi
  [ "$attempt" -lt 3 ] && sleep 20
done

if [ -z "$ok" ]; then
  if grep -q "rate-limited" logcat-*.txt 2>/dev/null; then
    echo "NOTE: rate-limited, so this run says NOTHING about the trust path - re-run it"
  fi
  echo "$SELFTEST never reported PASS"
  exit 1
fi
echo "$SELFTEST PASS on $(adb shell getprop ro.build.version.release | tr -d '\r')"
