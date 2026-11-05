#!/bin/sh
# Runs a test application in the Android emulator: installs it on the
# device ANDROID_SERIAL names (else the only one), writes the host's
# MAUL_RHI_* variables to its files/environment with run-as (the
# application is debuggable), starts its activity, and waits, four
# minutes at most from the start, for the closing line files/out ends
# with, "result: N failures". The library never ends the process, so the
# runner stops it. Passes when the line says 0 failures and the system
# did not restart meanwhile; otherwise shows the application's crashes
# from the log, and a restart of the system apart from the test's
# failures.
#
# adb can lose its connection to the device ("error: closed"). The steps
# before the start are idempotent, so each is tried up to three times,
# and each answer is read whole before its carriage returns are
# stripped: a lost read before the start ends the run (set -e), where a
# pipe would give its last command's status and an empty answer.
# Stopping and removing the application after the verdict cannot
# change it.
set -eu
adb=${ADB:-adb}
apk=$1
package=$2

# An idempotent step, tried up to three times while it fails.
again() {
    tries=1
    until "$@"; do
        [ "$tries" -lt 3 ] || return 1
        tries=$((tries + 1))
        sleep 1
    done
}

# An idempotent adb step's output, tried up to three times while adb
# fails: only the try that succeeded is kept, so an answer a lost try
# printed before its connection dropped is not given twice.
ask() {
    tries=1
    until output=$("$adb" "$@"); do
        [ "$tries" -lt 3 ] || return 1
        tries=$((tries + 1))
        sleep 1
    done
    printf '%s\n' "$output"
}

variables=$(env | grep '^MAUL_RHI_[A-Z0-9_]*=' || true)
# Writes the variables whole on each try, the file replaced, and removes
# any result an earlier run left. Only again calls it, which the
# linter cannot follow.
# shellcheck disable=SC2317
write_environment() {
    { [ -z "$variables" ] || printf '%s\n' "$variables"; } |
        "$adb" shell "run-as $package sh -c 'mkdir -p files && rm -f files/out && cat > files/environment'"
}

# Each run starts with the application fresh: one an earlier run left,
# as when its uninstall was lost, is removed with its data first. Its
# data is not cleared with pm clear instead: the media provider handles
# a package's clearing and removal in its own time, from the broadcast,
# and orphans the documents the package owns then, the ones a run made
# since among them. So the runner waits, 60 s at most, until the system
# has delivered its broadcasts, the removal's and the install's, before
# the test starts.
installed=$(ask shell pm list packages "$package")
if printf '%s\n' "$installed" | tr -d '\r' | grep -qx "package:$package"; then
    again "$adb" uninstall "$package" > /dev/null
fi
again "$adb" install -r "$apk" > /dev/null
if ! timeout 60 "$adb" shell am wait-for-broadcast-idle > /dev/null; then
    echo "the system's broadcasts were not seen idle in 60 s"
fi
# Should the removal have been lost too, install -r keeps the data,
# files/out among it: the environment write removes it, and the device
# is asked whether it is gone. Anything but "absent" (run-as failing
# among it) fails the test, whose result could be the earlier one.
again write_environment
gone=$(ask shell "run-as $package sh -c 'if [ -e files/out ]; then echo present; else echo absent; fi'")
gone=$(printf '%s' "$gone" | tr -d '\r')
if [ "$gone" != absent ]; then
    echo "an earlier run's result may be left: the device answered '$gone'"
fi
server=$(ask shell pidof system_server)
server=$(printf '%s' "$server" | tr -d '\r')
again "$adb" logcat -c
# The four minutes for the result count from the start, whatever the
# steps before it took.
launched=$(date +%s)
again "$adb" shell am start -W -n "$package/android.app.NativeActivity" > /dev/null
out=$(mktemp)
while [ $(($(date +%s) - launched)) -lt 240 ]; do
    "$adb" shell run-as "$package" cat files/out > "$out" 2> /dev/null || true
    grep -q '^result: ' "$out" && break
    sleep 1
done
cat "$out"
# The system's crash takes the test with it: said as such. A pid that
# cannot be read, the system down or adb lost, fails the test as a
# restart would.
if server_now=$(ask shell pidof system_server); then
    server_now=$(printf '%s' "$server_now" | tr -d '\r')
else
    server_now=
fi
restarted=
if [ "$server_now" != "$server" ]; then
    restarted=1
    echo "the system restarted during the test: system_server was pid $server, now ${server_now:-not read}"
fi
status=1
if [ -z "$restarted" ] && [ "$gone" = absent ] && grep -qx 'result: 0 failures' "$out"; then
    status=0
else
    "$adb" logcat -d -s AndroidRuntime:E DEBUG:F libc:F vulkan:E | tail -60 || true
fi
again "$adb" shell am force-stop "$package" || true
again "$adb" uninstall "$package" > /dev/null || true
rm -f "$out"
exit $status
