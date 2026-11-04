#!/bin/sh
# Runs a test application in the Android emulator: installs it on the
# device ANDROID_SERIAL names (else the only one), writes the host's
# MAUL_RHI_* variables to its files/environment with run-as (the
# application is debuggable), starts its activity, and waits, four
# minutes at most, for the closing line files/out ends with, "result: N
# failures". The library never ends the process, so the runner stops
# it. Passes when the line says 0 failures; otherwise shows the
# application's crashes from the log.
#
# adb can lose its connection to the device ("error: closed"). The steps
# before the start are idempotent, so each is tried up to three times.
# The application's data is cleared before its environment is written:
# install -r keeps it, so an earlier run whose uninstall was lost would
# leave its files/out, whose result line would be read as this run's.
# Stopping and removing the application after the verdict cannot
# change it.
set -eu
adb=${ADB:-adb}
apk=$1
package=$2
start=$(date +%s)

# An idempotent step, tried up to three times while it fails.
again() {
    tries=1
    until "$@"; do
        [ "$tries" -lt 3 ] || return 1
        tries=$((tries + 1))
        sleep 1
    done
}

variables=$(env | grep '^MAUL_RHI_[A-Z0-9_]*=' || true)
# Writes the variables whole on each try, the file replaced. It runs
# only through again, which shellcheck cannot follow.
# shellcheck disable=SC2317
write_environment() {
    { [ -z "$variables" ] || printf '%s\n' "$variables"; } |
        "$adb" shell "run-as $package sh -c 'mkdir -p files && cat > files/environment'"
}

again "$adb" install -r "$apk" > /dev/null
again "$adb" shell pm clear "$package" > /dev/null
again write_environment
again "$adb" logcat -c
again "$adb" shell am start -W -n "$package/android.app.NativeActivity" > /dev/null
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
    "$adb" logcat -d -s AndroidRuntime:E DEBUG:F libc:F vulkan:E | tail -60 || true
fi
again "$adb" shell am force-stop "$package" || true
again "$adb" uninstall "$package" > /dev/null || true
rm -f "$out"
exit $status
