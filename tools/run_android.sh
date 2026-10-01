#!/bin/sh
# Runs a test executable in the Android emulator (mrhi-0017), as CTest's
# cross-compiling emulator (cmake/android-emulator.cmake):
#
#   tools/run_android.sh <executable> [arguments...]
#
# Pushes it to /data/local/tmp/maul-rhi/ on the device ANDROID_SERIAL
# names (else the only one), runs it there with the host's MAUL_RHI_*
# variables, which adb's shell does not carry, and exits with its status.
set -eu
adb=${ADB:-adb}
exe=$1
shift
dir=/data/local/tmp/maul-rhi
name=$(basename "$exe")
"$adb" shell mkdir -p "$dir" > /dev/null
"$adb" push "$exe" "$dir/$name" > /dev/null 2>&1
quoted=""
for arg in "$@"; do
    quoted="$quoted '$(printf '%s' "$arg" | sed "s/'/'\\\\''/g")'"
done
variables=$(env | grep '^MAUL_RHI_[A-Z0-9_]*=' | sed "s/=\(.*\)/='\1'/" | tr '\n' ' ' || true)
# adb's shell protocol carries the remote exit status.
set +e
"$adb" shell "cd $dir && $variables./$name$quoted"
status=$?
"$adb" shell rm -f "$dir/$name" > /dev/null
exit $status
