# Debugging

How to find out what went wrong in a failing test, a driver or a frame.

## A broken invariant

In builds without `NDEBUG` (Debug, the sanitizer presets, fuzzing), `MRHI_ASSERT` stops the process with a trap at the broken invariant, where a debugger shows the state. The test driver traps the same way when the core breaks the SPI: work asked of a lost device, a handle it never made, a submitted frame that does not hold together. Run the test under a debugger, or read the sanitizer's report, at the trap's frame.

## Vulkan

- **The validation layer:** `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`, with `VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true` for synchronization validation, which checks the barriers the frame graph planned. The test presets set both, and CTest fails a test whose output contains "Validation Error".
- **Lavapipe:** `LVP_POISON_MEMORY=true` makes a read of memory nothing wrote fail reliably, which catches a missing store or an aliasing barrier that came too late. `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json` (or the path on the machine) restricts the loader to lavapipe on a machine with other GPUs.
- **LeakSanitizer:** the Vulkan loader normally unloads drivers when an instance ends, which turns their memory into leaks; the tests set `VK_LOADER_DISABLE_DYNAMIC_LIBRARY_UNLOADING=1`.
- **ThreadSanitizer** cannot see lavapipe's, LLVM's and the XCB client library's threads and locks ordered; `test/tsan.supp` leaves them alone.
- **RenderDoc** captures a run with labels where `VK_EXT_debug_utils` is present.

## Direct3D 12

The tests' executables export `D3D12SDKVersion` and `D3D12SDKPath` (`test/d3d12_debug.c`), so they run on the Agility SDK placed in `D3D12/` beside them when it is there, and on the system's runtime otherwise. `MAUL_RHI_D3D12_DEBUG=1` turns on the debug layer with GPU-based validation and fails a test on any error message. PIX shows labels.

GPU-based validation's resource state tracking reported false errors on the 1.619 Agility SDK with WARP ([#7](https://github.com/Rawframe-Project/maul-rhi/issues/7)): once queues, heaps, pipelines and resources had been made and released on a device, which every device the library opens does on D3D12's one device per adapter, it sometimes reported a texture moved to unordered access as still in `COMMON` at a dispatch (message 1358, "Incompatible texture barrier layout"), though the dispatch wrote the right values. 1.620.1 no longer does: 0 of 300 runs of `d3d12_uav_check 20 8` against 24 of 300 on 1.619.5, while it still reports the error when the barrier is really missing, so CI runs the validation whole. `test/d3d12_uav_check.c` reproduces it without library code: built as `d3d12_uav_check` by the `windows-d3d12` preset, it runs rounds of the same commands on one device, and the nightly `windows-d3d12-canary` runs `d3d12_uav_check 20 8` with `MAUL_RHI_D3D12_DEBUG=1` a hundred times on the pinned SDK. Run it the same way on a new Agility SDK before moving `tools/ci_agility.sh` to it, with `d3d12_indirect_check` below.

The 1.620.1 Agility SDK's GPU-based validation refuses one legal buffer transition: after a buffer moves from `INDIRECT_ARGUMENT` alone to `INDIRECT_ARGUMENT | NON_PIXEL_SHADER_RESOURCE`, `Close` returns `E_INVALIDARG` with no message, and the device stays. The driver never asks for that move (`src/d3d12_barrier.c`). `test/d3d12_indirect_check.c` closes a list after each of a few transition chains on one buffer, without library code: the refused move, the moves the driver records, and their neighbours. Built as `d3d12_indirect_check` by the `windows-d3d12` preset, it prints each chain's result with `MAUL_RHI_D3D12_DEBUG=1`, and exits 1 when any chain does not close. Run it on a new Agility SDK. Once the refused move closes there, the driver's indirect state can drop the non-pixel shader resource state again.

## Metal

`MTL_DEBUG_LAYER=1` and `MTL_SHADER_VALIDATION=1` stop a test on API misuse and out-of-bounds shader access. On the macOS 26 runner image (26.6.2, the Apple Paravirtual device) the texture usage check of shader validation reports "Texture usage flags mismatch" on every texture a shader reads or samples, in render and compute alike, and the access reads zero; `test/metal_usage_check.m` reproduces it without library code, and `MTL_SHADER_VALIDATION_TEXTURE_USAGE=0` turns that one check off. Xcode's GPU capture shows labels.

## The web

The web runner (`test/web_runner.cjs`) prints the page's console and fails on any error the browser logs; each device's lifetime error scope catches WebGPU validation errors the page would otherwise swallow. Run a test with `--worker` to run it in a dedicated worker. Chrome's own `chrome://gpu` and the WebGPU developer tools show what the browser's adapter offers.

## A frame that looks wrong

`mrhiPass_IsKept`, `mrhiPass_GetPlan`, `mrhiResource_GetPlan`, `mrhiDevice_GetFrameBarriers` and `mrhiDevice_GetFrameUsage` show what the compile decided: which passes were culled, which stores were kept, every barrier with its states, where each transient was placed and the memory they take. On the test driver, `mrhiTestAdapter.frameLog` counts what the frame walk found in each submitted frame.

## Fuzzing

Build with `-DMAUL_RHI_FUZZ=ON` and Clang, then run `fuzz_container`, `fuzz_cache`, `fuzz_pipeline` or `fuzz_vulkan_cache` (which needs a Vulkan adapter, lavapipe will do) over a corpus directory, always with `-max_total_time`. `tools/container_seed.py <dir>` writes `fuzz_container`'s seeds: every container the tests and samples compile in, and a minimal one per feature of the format. A crash leaves its input in the working directory; run the target on that file alone to reproduce it, then add the minimized input as a test in the module's suite. A finding on CI shows only the target, the kind of the finding and the sanitizer report, never the input, its file name or the seed, so reproduce it by fuzzing the target locally.
