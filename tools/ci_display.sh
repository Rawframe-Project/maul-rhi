#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Starts the display server a CI cell's surfaces present to and writes
# its variables to $GITHUB_ENV, so the later steps of the job find it:
#
#   tools/ci_display.sh x11       Xvfb on :99
#   tools/ci_display.sh wayland   weston's headless compositor
#
# The server lives until the job ends. Locally, tools/check.sh starts
# its own and stops it after the run.
set -euo pipefail

env_file=${GITHUB_ENV:?this script is for CI; tools/check.sh starts its own server}

case "${1:-}" in
    x11)
        setsid Xvfb :99 -screen 0 1920x1080x24 -nolisten tcp >/dev/null 2>&1 &
        for _ in $(seq 1 40); do
            [ -S /tmp/.X11-unix/X99 ] && break
            sleep 0.25
        done
        [ -S /tmp/.X11-unix/X99 ] || { echo "Xvfb did not start" >&2; exit 1; }
        echo "DISPLAY=:99" >>"$env_file"
        ;;
    wayland)
        runtime=$(mktemp -d)
        chmod 700 "$runtime"
        XDG_RUNTIME_DIR=$runtime setsid weston --backend=headless --socket=wayland-ci \
            --idle-time=0 >"$runtime/weston.log" 2>&1 &
        for _ in $(seq 1 40); do
            [ -S "$runtime/wayland-ci" ] && break
            sleep 0.25
        done
        [ -S "$runtime/wayland-ci" ] || { cat "$runtime/weston.log" >&2; exit 1; }
        echo "XDG_RUNTIME_DIR=$runtime" >>"$env_file"
        echo "WAYLAND_DISPLAY=wayland-ci" >>"$env_file"
        ;;
    *)
        echo "usage: tools/ci_display.sh x11|wayland" >&2
        exit 2
        ;;
esac
