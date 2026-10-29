# Conformance

How the library proves that every driver does the same thing, and that the core refuses exactly what WebGPU refuses. The testing rules that apply to every change are in [AGENTS.md](../../AGENTS.md#testing); this page explains the two suites they lean on.

## The conformance suite

`test/test_conformance.c` is the specification of a driver. It runs the same named cases through the public API on the test driver and on every native adapter the build's driver finds, with shader containers made offline from `test/shaders/` and committed as bytes, so CI needs no shader tools. Its cases fall in ten categories: `api`, `capabilities`, `binding`, `hazards`, `transfers`, `swapchain`, `loss`, `threads`, `limits` and `validation`.

- `test_conformance --list` prints the cases, `--case <name>` runs one, and every run ends with each case's outcome.
- `loss.simulated` loses a device with `mrhiDevice_SimulateLoss` and checks every answer owed. `threads.recording` records two passes at once on two threads; the web builds, which have no threads, interleave them on one.
- `swapchain.present` presents to a real window: an XCB window it makes itself where the XCB client library is installed, a Win32 window, a `CAMetalLayer` outside any window, the web runner's canvas, or Android's NativeActivity window.
- `validation.clean` requires the validation layer to have counted no fault, in builds that have it.
- The `MAUL_RHI_REQUIRE_*` variables turn what would be a skip into a failure: a driver with no adapter, a missing surface, heaps or pipeline statistics not granted, or the outside-driver path not built. CI sets them for what each runner has, so a runner that changes cannot quietly skip cases.

A case that every driver must pass is added here rather than to a per-module suite: per-module suites check the core's refusals on the test driver, the conformance suite checks that a driver does what the core promised.

On Linux CI the suite runs on lavapipe under the Khronos validation layer with synchronization validation, which checks the barriers the graph makes; any "Validation Error" in a test's output fails it. Lavapipe poisons new memory there (`LVP_POISON_MEMORY`), so reading memory nothing wrote fails reliably. D3D12 runs on WARP under the Agility SDK's debug layer with GPU-based validation, whose errors fail the suite; Metal under API and shader validation; WebGPU in headless Chrome, where any error the browser logs fails it.

## Mustpass lists

A driver built outside the tree is admitted on a run that passes every case of the current SPI version's list, `conformance/mustpass/spi-<N>.txt`. The `mustpass` test compares that list with the suite's `--list`, so a case added, renamed or dropped must be recorded there. Raising the SPI version renames the list; only the current version has one.

Without an outside driver, a Linux build hands the suite its own Vulkan driver through the outside-driver path (`test_conformance_external`), so that path is exercised in CI. `test/external/` builds a minimal driver against an install of the library alone.

## The WebGPU CTS as reference cases

The core refuses on every driver what breaks WebGPU's rules, and the WebGPU conformance test suite (CTS) states those rules as small tables of calls and outcomes, maintained with the specification. The CTS itself tests a WebGPU implementation, which the library only uses, so it is not run against the library. Instead `test/test_cts.c` translates the CTS validation cases that have Maul RHI counterparts into a table-driven test on the test driver: each case names its CTS test, the call it becomes and the outcome the CTS expects. The CTS revision is pinned in the file, whose header carries the CTS's BSD-3-Clause notice for the translated cases.

A case the library decides otherwise is listed in the test with its reason. Three rules differ, each on purpose:

- A layout's `bytesPerRow` or `rowsPerImage` of 0 means absent, where WebGPU's 0 is a value; the WebGPU driver passes it as undefined.
- An index range is whole indices; WebGPU takes any bytes.
- A query is written at most once a frame, where WebGPU allows once a pass, so Vulkan never meets a query written twice between resets.

Cases needing data a buffer cannot have (a size not a multiple of 4, where that changes the outcome) are counted, not run. Both counts are pinned in the test, so a change in either is seen. Translating the copy cases found one real difference: WebGPU allows empty copies, which Vulkan refuses, so empty copies, writes and reads now record nothing.

## Samples and the guide

`samples/` holds small programs, each checking its own result, so CTest runs them as integration tests on the build's native driver and in headless Chrome on the web. `test/test_guide.c` builds and runs the C snippets of `docs/user/` exactly as written, and `tools/check_guide.py` fails when a snippet drifts from it.
