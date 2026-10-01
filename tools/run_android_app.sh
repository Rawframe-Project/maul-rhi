#!/bin/sh
# Runs a test application in the Android emulator (mrhi-0017): installs
# it on the device ANDROID_SERIAL names (else the only one), writes the
# host's MAUL_RHI_* variables to its files/environment with run-as (the
# application is debuggable), starts its activity, and waits, four
# minutes at most, for the closing line files/out ends with, "result: N
# failures". The library never ends the process, so the runner stops
# it. Passes when the line says 0 failures; otherwise shows the
# application's crashes from the log.
set -eu
adb=${ADB:-adb}
apk=$1
package=$2
start=$(date +%s)
"$adb" install -r "$apk" > /dev/null
{ env | grep '^MAUL_RHI_[A-Z0-9_]*=' || true; } |
    "$adb" shell "run-as $package sh -c 'mkdir -p files && cat > files/environment'"
"$adb" logcat -c
"$adb" shell am start -W -n "$package/android.app.NativeActivity" > /dev/null
out=$(mktemp)
while [ $(($(date +%s) - start)) -lt 240 ]; do
    "$adb" shell run-as "$package" cat files/out > "$out" 2> /dev/null || true
    grep -q '^result: ' "$out" && break
    sleep 1
done
cat "$out"
status=1
if grep -qx 'result: 0 failures' "$out"; then
    status=0
else
    "$adb" logcat -d -s AndroidRuntime:E DEBUG:F libc:F vulkan:E | tail -60
fi
"$adb" shell am force-stop "$package"
"$adb" uninstall "$package" > /dev/null
rm -f "$out"
exit $status
