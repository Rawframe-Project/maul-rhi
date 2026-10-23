#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The checks, in three tiers. Each tier includes the one before it.
#
#   tools/check.sh changed   the files changed against origin/main: the
#                            source rules, the contract's generated files,
#                            formatting, the documentation checks, and the
#                            linux-clang-20-asan preset's tests without the
#                            integration label (the test driver, no GPU).
#                            Under two minutes; run it before every push.
#   tools/check.sh pr        what the pull request check runs on Linux:
#                            every check, clang-tidy, the linux-clang-20-asan
#                            and linux-gcc-14 presets with their integration
#                            tests on lavapipe and Xvfb, the seam check on
#                            X11, and a short fuzzing pass.
#   tools/check.sh full      also ThreadSanitizer, the shared library,
#                            labels compiled out, the seam check on Wayland
#                            and longer fuzzing.
#
# The builds are the CI cells' CMake presets (CMakePresets.json), in
# out/<preset>; the other trees go to out/check-*. Needs gcc-14, clang-20,
# clang-tidy-20, ninja, the clang-format version CI pins, lavapipe, the
# Vulkan validation layers, the XCB client library, Xvfb and, for full,
# weston: the organization's CI image has them all. The web, macOS,
# Windows, Android and iOS cells run in CI only. The org documentation
# check runs when the organization's .github checkout is at ORG_DEFAULTS
# (default: ../../.github from the repository).
set -euo pipefail

tier=${1:-changed}
case "$tier" in
changed | pr | full) ;;
*)
    echo "usage: tools/check.sh [changed|pr|full]" >&2
    exit 2
    ;;
esac

cd "$(dirname "$0")/.."
root=$PWD
out=$root/out
mkdir -p "$out"
org=${ORG_DEFAULTS:-$root/../../.github}
jobs=${JOBS:-8}

step() { printf '\n== %s\n' "$*"; }

# A tree outside the presets (clang-tidy, fuzzing).
configure() { # name, then cmake arguments
    local name=check-$1
    shift
    cmake -S "$root" -B "$out/$name" -G Ninja -DMAUL_RHI_WERROR=ON "$@" >"$out/$name.log"
}

build() { cmake --build "$out/check-$1" -j "$jobs" "${@:2}" >>"$out/check-$1.log"; }

# A CI cell: its configure, build and test presets, the tests under an X
# server of their own. More ctest arguments (a label filter) follow the
# preset's name.
preset() {
    local name=$1
    shift
    step "preset $name"
    cmake --preset "$name" >"$out/$name.log"
    cmake --build --preset "$name" -j "$jobs" >>"$out/$name.log"
    xvfb-run -a ctest --preset "$name" -j "$jobs" "$@"
}

source_checks() {
    step "source, module, length, documentation and snippet rules"
    python3 tools/check_source.py
    python3 tools/check_modules.py
    python3 tools/check_lengths.py
    python3 tools/check_docs.py
    python3 tools/check_guide.py
    python3 tools/gen_contract.py --check
    if [ -f "$org/scripts/docs_check.py" ]; then
        python3 "$org/scripts/docs_check.py" --exclude khronos --exclude directx
    else
        echo "skipped the organization's documentation check: no checkout at $org"
    fi
}

all_c_files() {
    git ls-files 'include/*.h' 'src/*.[chm]' 'test/*.[chm]' 'tools/*.[ch]' 'bench/*.[ch]' 'samples/*.[chm]'
}

if [ "$tier" = changed ]; then
    base=$(git merge-base HEAD origin/main 2>/dev/null || echo HEAD)
    mapfile -t changed < <({
        git diff --name-only "$base"
        git ls-files --others --exclude-standard
    } | sort -u)
    if [ ${#changed[@]} -eq 0 ]; then
        echo "nothing changed against origin/main"
        exit 0
    fi
    source_checks
    mapfile -t c_files < <(printf '%s\n' "${changed[@]}" | grep -E '\.[chm]$' | grep -v '^src/generated/' || true)
    present=()
    for f in "${c_files[@]+"${c_files[@]}"}"; do [ -f "$f" ] && present+=("$f"); done
    if [ ${#present[@]} -gt 0 ]; then
        step "clang-format on ${#present[@]} changed files"
        clang-format --dry-run -Werror "${present[@]}"
    fi
    if ! printf '%s\n' "${changed[@]}" | grep -qvE '\.md$|^docs/|^LICENSE$|^\.github/'; then
        echo "only documentation changed: no build"
        exit 0
    fi
    preset linux-clang-20-asan -LE integration
    exit 0
fi

source_checks

step "clang-format on every file"
clang-format --dry-run -Werror $(all_c_files)

step "clang-tidy"
# The web sources need Emscripten's sysroot, the D3D12 ones Windows' headers
# and the Metal ones Apple's: CI checks those in their own cells.
configure tidy -DCMAKE_C_COMPILER=clang-20 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
for f in $(grep -L '"web_js.h"' src/*.c | grep -v d3d12); do
    clang-tidy-20 -p "$out/check-tidy" --quiet --warnings-as-errors='*' "$f" 2>/dev/null
done

preset linux-clang-20-asan
preset linux-gcc-14
preset linux-seam-x11

fuzz_seconds=10
[ "$tier" = full ] && fuzz_seconds=60
step "fuzzing, $fuzz_seconds seconds per target"
configure fuzz -DCMAKE_C_COMPILER=clang-20 -DMAUL_RHI_FUZZ=ON -DMAUL_RHI_BUILD_TESTS=OFF \
    -DMAUL_RHI_BUILD_SAMPLES=OFF -DMAUL_RHI_BUILD_BENCH=OFF
build fuzz
python3 tools/container_seed.py "$out/corpus/fuzz_container" >/dev/null
for target in "$out"/check-fuzz/fuzz_*; do
    name=$(basename "$target")
    corpus=$out/corpus/$name
    mkdir -p "$corpus"
    "$target" -max_total_time=$fuzz_seconds "$corpus" >"$out/$name.log" 2>&1 ||
        {
            tail -30 "$out/$name.log"
            exit 1
        }
    echo "$name: no finding"
done

[ "$tier" = full ] || exit 0

for name in linux-clang-20-tsan linux-shared linux-clang-20; do
    preset "$name"
done

step "preset linux-seam-wayland, on weston's headless compositor"
cmake --preset linux-seam-wayland >"$out/linux-seam-wayland.log"
cmake --build --preset linux-seam-wayland -j "$jobs" >>"$out/linux-seam-wayland.log"
runtime=$(mktemp -d)
XDG_RUNTIME_DIR=$runtime weston --backend=headless --socket=wayland-check --idle-time=0 \
    >"$out/weston.log" 2>&1 &
weston=$!
trap 'kill "$weston" 2>/dev/null || true; rm -rf "$runtime"' EXIT
for _ in $(seq 1 40); do
    [ -S "$runtime/wayland-check" ] && break
    sleep 0.25
done
env -u DISPLAY XDG_RUNTIME_DIR="$runtime" WAYLAND_DISPLAY=wayland-check \
    ctest --preset linux-seam-wayland

step "all checks passed"
