# Development

Setting up a machine to build and test Maul RHI, and regenerating what is generated.

## Toolchains

| Target | Needs |
|---|---|
| Linux | CMake 3.25, Ninja, GCC 14 and Clang 20 (`gcc-14`, `clang-20`; a system GCC 13 is refused), `clang-tidy-20`, clang-format 22.1.5 (`pip install clang-format==22.1.5`), and to run the tests lavapipe (`mesa-vulkan-drivers`), the Vulkan validation layers, the XCB client library and Xvfb; weston for the Wayland seam check. |
| Windows | Visual Studio with "C++ Clang tools for Windows" (`clang-cl`); the Agility SDK for the tests. |
| macOS, iOS | Xcode's Apple Clang; a booted iOS simulator for the iOS tests. |
| Android | The NDK r28 or newer (`ANDROID_NDK_HOME`), the SDK with an x86_64 Android 15 system image, and KVM for the emulator. |
| The web | Emscripten, the version `ci.yml` pins for `web-emscripten` (`EMSDK` set, as `emsdk activate` does), or Clang 20 with `lld-20`, wasi-libc and `libclang-rt-20-dev-wasm32`; Node 20 or newer; Puppeteer for headless Chrome, as `test/browser`'s lockfile pins it (copy `test/browser/package*.json` to `<dir>`, run `npm ci --prefix <dir>`, then set `MRHI_NODE_MODULES=<dir>/node_modules`). |

The organization's CI image has everything the Linux and WASI cells need. On the shared development machine, run full checks, large builds, fuzzing and emulators through `/home/rawframe-project/tools/heavy.sh`.

## Building

Every CI cell is a preset of `CMakePresets.json`, built in `out/<preset>`:

```sh
cmake --preset linux-clang-20-asan
cmake --build --preset linux-clang-20-asan
xvfb-run -a -s "-screen 0 1280x1024x24 -noreset" ctest --preset linux-clang-20-asan
```

The Linux test presets require lavapipe, the validation layer and an X server, and fail rather than skip without them. The server runs with `-noreset`, which keeps it from resetting, and refusing connections, whenever its last client leaves. A plain build needs none of that:

```sh
cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang-20
cmake --build build
ctest --test-dir build -LE integration   # the test driver only, no GPU
```

The options a developer uses besides the user's: `MAUL_RHI_SANITIZE` (AddressSanitizer and UBSan), `MAUL_RHI_TSAN`, `MAUL_RHI_FUZZ` (the libFuzzer targets, Clang only), `MAUL_RHI_COVERAGE`, `MAUL_RHI_SEAM` (the check against Maul Window, which fetches it at configure time), `MAUL_RHI_CONFORMANCE_DRIVER` (a target defining an outside driver for the conformance suite), `MAUL_RHI_VULKAN_SPLIT_LANES` (the Vulkan driver's work submitted in a lane per pass class, for the `linux-lanes` cell) and `MAUL_RHI_D3D12_AGILITY` (the Agility SDK's `bin/x64` folder, copied into `D3D12/` beside the tests).

## Platform runs

- **Android.** Boot an emulator with SwiftShader (`emulator -avd <avd> -no-window -no-audio -gpu swiftshader_indirect`), then `cmake --preset android-emulator` and `ctest --preset android-emulator`. Tests run on the device `ANDROID_SERIAL` names through `tools/run_android.sh`, which pushes each executable over adb and passes the host's `MAUL_RHI_*` variables, which adb's shell does not carry. The conformance suite also runs as an application on the system's NativeActivity (`tools/build_android_app.sh`, `tools/run_android_app.sh`), presenting to its window. Stop the emulator you started when the run ends.
- **iOS.** Boot a simulator, set `MWIN_IOS_DEVICE` to its UDID (or leave one booted), then use the `ios-simulator` presets; tests run through `xcrun simctl spawn`.
- **Windows.** Unpack the Agility SDK's NuGet package (`Microsoft.Direct3D.D3D12`, the version `tools/ci_agility.sh` pins) into `out/agility`, as `tools/ci_agility.sh` does with the download's digest checked; the `windows-d3d12` preset copies its DLLs beside the tests.
- **The web.** `cmake --preset web-emscripten` (with `EMSDK` set) or `--preset web-wasi`, then `ctest` with `MRHI_NODE_MODULES` naming Puppeteer's modules.

## Generated files

| Files | Generator | Inputs | In CI |
|---|---|---|---|
| `include/maul-rhi/*.h`, `src/generated/{capabilities,defaults,diagnostic_texts,result_names}.c` | `python3 tools/gen_contract.py` | `contract/mrhi.json` | regenerated, and any difference fails |
| `test/shaders/*_container.h`, `samples/shaders/*_container.h` | `python3 tools/gen_test_shaders.py` | the GLSL and WGSL sources and JSON reflections beside them, the generator and writer scripts, and the contract's values the writer reads | committed and stamped; CI has no shader tools and checks the stamps |
| `src/generated/d3d12_expand.h` | `python3 tools/gen_kernels.py [dxc]` | `src/d3d12_expand.hlsl` and the generator | committed and stamped; CI has no DXC and checks the stamp |
| `maul-rhi.mjs` (in the build tree) | `tools/gen_web_glue.py`, run by the build | the `EM_JS` functions of the WebGPU driver | built every time |

`gen_test_shaders.py` needs `glslangValidator`, `spirv-link`, `spirv-val`, `spirv-cross` and `dxc` on the path, and runs `tools/mrhi_msl.py`, `tools/mrhi_dxil.py` and `tools/mrhi_container.py` for each container. `gen_kernels.py` signs the DXIL when DXC's validator (`libdxil`) sits beside it. Run a generator again whenever its inputs change, and commit what it writes.

Both generators make only with the pinned tools, compared exactly, and refuse others by name:

| Tool | Pinned version | Where it comes from |
|---|---|---|
| `glslangValidator` | 15.1.0 | Ubuntu 24.04's `glslang-tools` (15.1.0-2~ubuntu0.24.04.2) |
| `spirv-link`, `spirv-val` | SPIRV-Tools v2025.1 | Ubuntu 24.04's `spirv-tools` (2025.1~rc1-1~ubuntu0.24.04.2) |
| `spirv-cross` | 2021.01.15+1.3.239.0 | Ubuntu 24.04's `spirv-cross` |
| `dxc`, `libdxcompiler`, `libdxil` | v1.9.2607: libdxcompiler 1.9.0.1, commit 0d3ee6b5 | the release asset `linux_dxc_2026_07_29.x86_x64.tar.gz` of [microsoft/DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2607), SHA-256 `55665c87824051ed4774ff3280a79ccbbb7d39243b9736ca5e98222134112d54`; unpack it anywhere, put its `bin` on `PATH` and its `lib` on `LD_LIBRARY_PATH` |

Check the tarball's SHA-256 against that digest, which the release API reports for the asset, before running it. The Maul family pins the same DXC.

Each generated header carries a stamp: `// inputs sha256:` over every input it was made from (each named by its path, CRLF read as LF; for a container, also every value the writer reads from `contract/mrhi.json`, so renumbering one stales the containers while rewording the contract does not), a `// tool` line per pinned tool, and `// bytes sha256:` over its array. `--check` on either generator needs no tools: it fails on a changed input, a moved pin or an edited header, naming the header. `--self-test` proves that check on a copy of the tree: it passes as committed and with CRLF line ends, and fails on each of the three; for the containers it also fails when two of the contract's sample type values are swapped and passes when only the contract's prose changes. With `--skip-inputs`, which turns off the input comparison, the self-test exits 1. `tools/check.sh` and CI's `checks` job run all four. Moving a pin means regenerating every header with the new tool, in a pull request of its own that says why its bytes changed.

The library version in `base.h` is not generated: release-please moves it, and `gen_contract.py` keeps it.

## Vendored headers

`khronos/` and `directx/` hold the Khronos Vulkan and Video headers and the DirectX headers exactly as published, each with a README naming the source, the version and the files. An update replaces the files from a newer release and updates the README in the same pull request.

## Family files

The files listed under "Editing shared family files" in [AGENTS.md](../../AGENTS.md#ways-to-hurt-yourself) are synced from the organization's `.github` repository. Take a new family revision with

```sh
python3 /home/rawframe-project/.github/maul/family_sync.py --write maul-rhi
python3 /home/rawframe-project/.github/maul/family_sync.py --check maul-rhi
```

in a pull request of its own.
