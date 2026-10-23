# Development

Setting up a machine to build and test Maul RHI, and regenerating what is generated.

## Toolchains

| Target | Needs |
|---|---|
| Linux | CMake 3.25, Ninja, GCC 14 and Clang 20 (`gcc-14`, `clang-20`; a system GCC 13 is refused), `clang-tidy-20`, clang-format 22.1.5 (`pip install clang-format==22.1.5`), and to run the tests lavapipe (`mesa-vulkan-drivers`), the Vulkan validation layers, the XCB client library and Xvfb; weston for the Wayland seam check. |
| Windows | Visual Studio with "C++ Clang tools for Windows" (`clang-cl`); the Agility SDK for the tests. |
| macOS, iOS | Xcode's Apple Clang; a booted iOS simulator for the iOS tests. |
| Android | The NDK r28 or newer (`ANDROID_NDK_HOME`), the SDK with an x86_64 Android 15 system image, and KVM for the emulator. |
| The web | Emscripten (`EMSDK` set, as `emsdk activate` does), or Clang 20 with `lld-20`, wasi-libc and `libclang-rt-20-dev-wasm32`; Node 20 or newer; Puppeteer for headless Chrome (`npm install --prefix <dir> puppeteer`, then `MRHI_NODE_MODULES=<dir>/node_modules`). |

The organization's CI image has everything the Linux and WASI cells need. On the shared development machine, run full checks, large builds, fuzzing and emulators through `/home/rawframe-project/tools/heavy.sh`.

## Building

Every CI cell is a preset of `CMakePresets.json`, built in `out/<preset>`:

```sh
cmake --preset linux-clang-20-asan
cmake --build --preset linux-clang-20-asan
xvfb-run -a ctest --preset linux-clang-20-asan
```

The Linux test presets require lavapipe, the validation layer and an X server, and fail rather than skip without them. A plain build needs none of that:

```sh
cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang-20
cmake --build build
ctest --test-dir build -LE integration   # the test driver only, no GPU
```

The options a developer uses besides the user's: `MAUL_RHI_SANITIZE` (AddressSanitizer and UBSan), `MAUL_RHI_TSAN`, `MAUL_RHI_FUZZ` (the libFuzzer targets, Clang only), `MAUL_RHI_COVERAGE`, `MAUL_RHI_SEAM` (the check against Maul Window, which fetches it at configure time), `MAUL_RHI_CONFORMANCE_DRIVER` (a target defining an outside driver for the conformance suite) and `MAUL_RHI_D3D12_AGILITY` (the Agility SDK's `bin/x64` folder, copied into `D3D12/` beside the tests).

## Platform runs

- **Android.** Boot an emulator with SwiftShader (`emulator -avd <avd> -no-window -no-audio -gpu swiftshader_indirect`), then `cmake --preset android-emulator` and `ctest --preset android-emulator`. Tests run on the device `ANDROID_SERIAL` names through `tools/run_android.sh`, which pushes each executable over adb and passes the host's `MAUL_RHI_*` variables, which adb's shell does not carry. The conformance suite also runs as an application on the system's NativeActivity (`tools/build_android_app.sh`, `tools/run_android_app.sh`), presenting to its window. Stop the emulator you started when the run ends.
- **iOS.** Boot a simulator, set `MWIN_IOS_DEVICE` to its UDID (or leave one booted), then use the `ios-simulator` presets; tests run through `xcrun simctl spawn`.
- **Windows.** Unpack the Agility SDK's NuGet package (`Microsoft.Direct3D.D3D12`, the version `directx/README.md` names) into `out/agility`; the `windows-d3d12` preset copies its DLLs beside the tests.
- **The web.** `cmake --preset web-emscripten` (with `EMSDK` set) or `--preset web-wasi`, then `ctest` with `MRHI_NODE_MODULES` naming Puppeteer's modules.

## Generated files

| Files | Generator | Inputs | In CI |
|---|---|---|---|
| `include/maul-rhi/*.h`, `src/generated/{capabilities,defaults,diagnostic_texts,result_names}.c` | `python3 tools/gen_contract.py` | `contract/mrhi.json` | regenerated, and any difference fails |
| `test/shaders/*_container.h`, `samples/shaders/*_container.h` | `python3 tools/gen_test_shaders.py` | the GLSL and WGSL sources and JSON reflections beside them | committed; CI has no shader tools |
| `src/generated/d3d12_expand.h` | `python3 tools/gen_kernels.py [dxc]` | `src/d3d12_expand.hlsl` | committed; CI has no DXC |
| `maul-rhi.mjs` (in the build tree) | `tools/gen_web_glue.py`, run by the build | the `EM_JS` functions of the WebGPU driver | built every time |

`gen_test_shaders.py` needs `glslangValidator`, `spirv-link`, `spirv-val`, `spirv-cross` and `dxc` on the path, and runs `tools/mrhi_msl.py`, `tools/mrhi_dxil.py` and `tools/mrhi_container.py` for each container. `gen_kernels.py` signs the DXIL when DXC's validator (`libdxil`) sits beside it. Run a generator again whenever its inputs change, and commit what it writes.

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
