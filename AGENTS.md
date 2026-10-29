# Maul RHI

Maul RHI is a C library that puts one GPU API over Vulkan, Direct3D 12, Metal and WebGPU. A program finds adapters, opens devices, makes buffers, textures, shader containers and pipelines, and sends all work through a frame graph that plans the barriers and transient memory. It is part of the Maul family. The family rules are in [docs/internals/maul-conventions.md](docs/internals/maul-conventions.md), and [docs/internals/overview.md](docs/internals/overview.md) explains how the parts fit.

It never compromises on:

1. **WebGPU's rules everywhere.** The core refuses on every driver what WebGPU would refuse, so a frame that records anywhere records on the web. A driver never meets an unchecked call.
2. **No hazard can be written.** The public API has no barriers, states, fences or semaphores. Work reaches the GPU only through frames whose passes declare every access.
3. **No threads, no callbacks.** The library starts no thread and never calls the program. Everything that finishes later is a request, answered once in its owner's queue.
4. **Memory up front.** An instance and a device take all their memory from the caller's allocator when they are made, within named limits. Recording and submitting never allocate.
5. **Hostile input.** Shader containers, pipeline caches and every argument are validated and refused with a status in every build.

## Commands

```sh
tools/check.sh changed          # before every push: rules, contract, format, the asan preset without integration tests (< 2 min)
tools/check.sh pr               # the Linux part of the pull request check: clang-tidy, asan and gcc presets on lavapipe, seam on X11, short fuzzing
tools/check.sh full             # adds TSan, the shared library, labels compiled out, seam on Wayland, longer fuzzing
cmake --preset linux-clang-20-asan && cmake --build --preset linux-clang-20-asan   # one CI cell
xvfb-run -a -s "-screen 0 1280x1024x24 -noreset" ctest --preset linux-clang-20-asan -L draw   # one suite
./out/linux-clang-20-asan/test_conformance --case binding.draws  # one conformance case; --list lists them
python3 tools/gen_contract.py   # regenerate headers and src/generated/ after editing contract/mrhi.json
```

Run the `pr` and `full` tiers through the workspace's heavy-job wrapper on the shared machine. Every CI cell is a preset in `CMakePresets.json`, built in `out/<preset>`. The Linux test presets require lavapipe, the Vulkan validation layer and an X server, so run them under `xvfb-run`. Each suite `test/test_<name>.c` has the label `<name>`. Tests that drive a real GPU, window or browser also carry `integration`, and `-LE integration` leaves them out.

## Ways to hurt yourself

- **Killing by pattern.** Never `pkill -f`, `pgrep | kill` or `kill` a PID found by matching a name or path: other sessions build, fuzz and run emulators on the same machine with the same binary names. Kill only a PID you started.
- **Emulators and compositors left running.** The Android emulator, the iOS simulator, Xvfb and weston are started for one run. Stop the one you started when that run ends; `tools/check.sh` stops its own weston.
- **Endless fuzzing.** A libFuzzer target runs until it finds something. Always pass `-max_total_time`.
- **Editing generated files.** `include/maul-rhi/*.h` and `src/generated/{capabilities,defaults,diagnostic_texts,result_names}.c` and `src/generated/out_copies.h` come from `contract/mrhi.json` through `tools/gen_contract.py`; CI fails on any difference. Edit the contract and regenerate. The version macros in `base.h` are the exception: release-please moves them, and the generator keeps them.
- **Regenerating shader bytes without the tools.** `test/shaders/*_container.h`, `samples/shaders/*_container.h` and `src/generated/d3d12_expand.h` are committed because CI has no glslang, SPIRV-Cross or DXC. Regenerate them with `tools/gen_test_shaders.py` and `tools/gen_kernels.py` only with the pinned tools, as [development.md](docs/operations/development.md#generated-files) describes. Each header is stamped with its inputs, tools and bytes, and `--check` fails when a source or script changed without regenerating.
- **Editing vendored headers.** `khronos/` and `directx/` hold the Khronos and DirectX headers exactly as published. Each has a README naming the source, the version and the files. Replace them whole from a newer release, never patch them.
- **Editing shared family files.** These are synced from the canonical copies in the organization's `.github` repository and fail CI when they differ: `docs/internals/maul-conventions.md`; `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitattributes`, `.gitignore`; `cmake/CompilerFlags.cmake`, `cmake/Install.cmake`, `cmake/Library.cmake`, `cmake/ReadVersion.cmake`, `cmake/Testing.cmake`, `cmake/Config.cmake.in`, `cmake/pkg-config.pc.in`, `cmake/HeaderCheck/CMakeLists.txt`; `bench/CMakeLists.txt`; `test/test_harness.h`; and `tools/build_docs.py`, `check_docs.py`, `check_exports.py`, `check_guide.py`, `check_lengths.py`, `check_modules.py`, `check_source.py`, `check_test_hygiene.py`, `fuzz_target.sh`, `gen_api.py`, `run_quarantine.sh`, and `tools/test_hygiene/`. Change the canonical copy first.
- **The seam check's network.** `MAUL_RHI_SEAM=ON` fetches Maul Window from GitHub at configure time. Builds without it never touch the network.

## Invariants

- **The contract.** `contract/mrhi.json` is the source of truth for the public API. Every function, struct, enum and bitflags maps onto the four APIs or names why it is the library's own; the generator refuses one that does neither. [contract.md](docs/internals/contract.md) has the schema.
- **Versions.** The library version lives in `include/maul-rhi/base.h` between the release-please markers; only release-please changes it. `MRHI_ABI_VERSION`, made from it, stamps the instance def (the root def), which the instance checks right after the cookie; other defs carry only cookies. `contract_version` in `contract/mrhi.json` names the contract's revision; only the tools read it. `MRHI_SPI_VERSION` in `src/driver.h` is the driver interface's version: any change to the installed SPI headers (`src/driver.h`, `command.h`, `reflection.h`, `container.h`, `container_interface.h`) or to a public type they reach, or to what a driver must do or may assume without a header edit, raises it and renames the mustpass list `conformance/mustpass/spi-<N>.txt` to the new number; only the current version has a list. `tools/check_spi_version.py` fails when the digest of what a driver sees moves without the version; `--write` records the new digest in `driver.h`.
- **The shader container format** has a version in its header, 2 today. Any change to its bytes raises it, in `src/container_table.c`, `tools/mrhi_container.py` and [shader-container-format.md](docs/user/shader-container-format.md) together.
- **One native driver per build:** Metal on Apple systems, D3D12 on Windows (or Vulkan), Vulkan elsewhere, WebGPU on the web. The test driver and the validation layer are build options beside it.
- **Drivers never call the core.** They answer only when the core polls, and read a submitted frame only through the read-only view in `src/driver.h`.
- **Modules:** `tools/modules.txt` lists which module may include which. A new include needs a line there. Driver files (`vulkan_*`, `d3d12_*`, `metal_*`, `webgpu_*`) never include each other across APIs.
- **Assertions:** `MRHI_ASSERT` (`src/invariant.h`) checks internal invariants only. It traps in builds without `NDEBUG` (Debug, sanitizers, fuzzing) and compiles out of Release. Never assert on caller input, container bytes or what a driver returns; validate and return a status.
- **The wasm budget:** the library with its WebGPU driver, at `-Oz`, stays within 128 KiB of wasm (`tools/wasm_size.py`, run by the web job).
- **Workflows:** every action is pinned by commit SHA with its version in a comment, and each workflow and job declares the least `permissions` it needs.

## Testing

Follow the [repository standard's Tests section](https://github.com/Rawframe-Project/.github/blob/main/docs/repository-standard.md#tests); in this repository:

- Most behavior is tested on the test driver (`maul-rhi/test.h`), which needs no GPU and walks every submitted frame as a driver would, trapping on any record it could not translate.
- A core test checks what a caller sees and what a driver is handed (the SPI: the frame view, its command records, staging and the readback ring, `mrhiTestFrameLog`), never the core's own bookkeeping in `mrhiDevice` or `mrhiInstance`. A white-box suite may set a counter to an edge no test reaches through public calls (a request id about to wrap, a repeat count about to saturate), and then checks only what the calls return.
- `test/test_conformance.c` is the driver specification: named cases, run on the test driver and on every native adapter. A driver behavior that every driver must share is a conformance case, not a per-suite test. A new case is added to `conformance/mustpass/spi-<N>.txt` of the current SPI version.
- A rule the core takes from WebGPU is proven by a case in `test/test_cts.c`, which translates the WebGPU CTS's validation cases and names each one.
- Every C snippet of `docs/user/` appears unchanged in `test/test_guide.c`; `tools/check_guide.py` enforces it.
- On CI the Linux tests run under the Vulkan validation layer with synchronization validation, and any "Validation Error" in a test's output fails it. D3D12 runs under the debug layer with GPU-based validation, without its state tracking on pull requests (#7), Metal under API and shader validation.
- A test that finds no adapter exits 77 and counts as skipped; CI fails on a skipped test, so each preset sets the `MAUL_RHI_REQUIRE_*` variables of what its runner has.
- A fuzz finding becomes a test in the module's suite with the minimized input.

## Documentation

Follow the [repository standard's Documentation section](https://github.com/Rawframe-Project/.github/blob/main/docs/repository-standard.md#documentation). A change in what a caller must do or can rely on updates the page of `docs/user/` for that part and, if it is in a snippet, the matching code in `test/test_guide.c`. A change in how a driver maps a concept updates the contract's mapping row and, when it is a trap, that driver's page in `docs/internals/`. The API reference and the mapping tables are generated into the documentation site; never commit them.

## Pull requests

Titles are conventional commits. Scopes: `contract`, `instance`, `device`, `resources`, `graph`, `encoder`, `binding`, `shader`, `pipeline`, `query`, `surface`, `diagnostics`, `spi`, `vulkan`, `d3d12`, `metal`, `webgpu`, `conformance`, `samples`, `bench`, `tools`, `build`, `family`. Example: `fix(d3d12): keep a buffer's read states across a native pass`.

A change to the contract, the container format or the SPI says in its body who sees it: programs, shader cooks, or drivers built outside the tree.
