# Checks

What to run before pushing, what each tier covers, and what the test presets expect of the machine.

## The tiers

`tools/check.sh` has three tiers; each includes the one before it.

| Tier | Runs | Budget |
|---|---|---|
| `changed` | The source, module, length, documentation, snippet and test hygiene rules; the contract's generated files; the organization's documentation check; clang-format on the changed files; the `linux-clang-20-asan` preset's tests without the `integration` label. A change to documentation alone builds nothing. | under 2 minutes |
| `pr` | Every rule; clang-format on every file; clang-tidy on the sources it can see on Linux; the `linux-clang-20-asan` and `linux-gcc-14` presets, integration tests included, on lavapipe under Xvfb; the seam check on X11; 10 seconds of fuzzing per target. | under 10 minutes |
| `full` | Also ThreadSanitizer, the shared library, labels compiled out, the Vulkan driver's lanes split (`linux-lanes`), the seam check on Wayland under weston's headless compositor, and 60 seconds of fuzzing per target. | under 20 minutes |

Run `changed` before every push. Run `pr` and `full` through the workspace's heavy-job wrapper on the shared machine:

```sh
/home/rawframe-project/tools/heavy.sh tools/check.sh full
```

Every preset the script configures is followed by `check_test_hygiene.py --ctest`, which fails a test without a label and a build that registers none; the tree scan rejects a sleep, the wall clock, a retry or a skip in the tests unless it is marked `// hygiene: <reason>`. The macOS, Windows, Android, iOS and web cells run in CI only. The organization's documentation check runs when the `.github` repository is checked out beside the workspace (`ORG_DEFAULTS` overrides where).

## Continuous integration

`.github/workflows/ci.yml` runs on every pull request, on `main` and every night. Its one required check, `required`, passes when every other job passed or was skipped.

| Job | What it runs |
|---|---|
| `changes` | Whether anything beyond documentation changed; the builds skip when not. |
| `docs`, `zizmor` | The organization's documentation check, from the `docs-check` action at the pinned commit (with `khronos/` and `directx/` left out), and the workflow audit. |
| `checks` | In the CI image: clang-format on every file, the rules below, the test hygiene check and every test's label, the contract's files, the documentation site, and clang-tidy on the native sources and, against `wasm32-wasi`, the web sources. |
| `build` | One cell per preset through the organization's `cpp.yml`, with a setup script run before configuring: the Linux presets in the CI image with Xvfb, or weston for `linux-seam-wayland` (`tools/ci_display.sh`); `web-wasi` in the image with Puppeteer's headless Chrome (`tools/ci_browser.sh`); `macos-clang` on macOS 15 and `macos-clang-debug` on macOS 26 after the Metal texture usage check (`tools/ci_metal.sh`); `windows-d3d12` with the Agility SDK, its download checked by digest (`tools/ci_agility.sh`), and `windows-vulkan`. `linux-lanes` builds the Vulkan driver with a lane per pass class (`MAUL_RHI_VULKAN_SPLIT_LANES`, [vulkan.md](../internals/vulkan.md#frames)). On a pull request `linux-clang-20-asan` runs each test up to three times, so a flaky one fails before it merges. |
| `msvc-headers` | The public headers alone under MSVC's `cl`, as C17 and C++17 with `/W4 /WX`. |
| `web-emscripten` | The `web-emscripten` preset on the runner with the emsdk action and a pinned Emscripten, and the library's size at `-Oz` against `tools/wasm_size.py`'s budget. |
| `fuzz` | Each fuzz target for 30 seconds on a pull request and 5 minutes on `main`, from its seeds, through `tools/fuzz_target.sh`: a finding fails the job with the target, the kind and count of its findings and the sanitizer report, never the input, its file name or the seed, which stay on the runner with the target's log. |
| `fuzz-targets`, `fuzz-nightly` | Nightly and by hand only: each fuzz target for 10 minutes in a job of its own, from its seeds and the corpus of the nights before. The corpus lives in an Actions cache per target, keyed by the target and its build's inputs; after the run the fuzzer's merge minimizes it with the seeds, which stay its floor, and saves it for the next night, unless the run found something. The run's summary gives the seeds' coverage beside the corpus's. Each target runs through `tools/fuzz_target.sh` as in `fuzz`, and no run uploads a corpus, a finding or a log. |
| `windows-d3d12-states` | Nightly and by hand only: the `windows-d3d12` cell with GPU-based validation's resource state tracking, which reports false errors in about one run in fifty ([#7](https://github.com/Rawframe-Project/maul-rhi/issues/7)), so it is not part of the pull request check and is never retried. |
| `android-emulator`, `ios-simulator` | Nightly and by hand only: the presets of those names in the Android emulator and the iOS simulator, which take minutes to boot. |

Two checks are off on pull requests. GPU-based validation's resource state tracking is off in `windows-d3d12` (`MAUL_RHI_D3D12_GBV_STATES=0`), since the Agility SDK reports false errors from it ([#7](https://github.com/Rawframe-Project/maul-rhi/issues/7), [debugging.md](debugging.md#direct3d-12)); the debug layer and the rest of the validation stay on, and `windows-d3d12-states` runs it nightly. And on macOS 26 shader validation's texture usage check is known to misfire: on macOS 26.6.2 (25G83, runner image 20261021), on the Apple Paravirtual device, it reports a mismatch on every texture a shader reads and zeroes the read, so `tools/ci_metal.sh` turns that check off for `macos-clang-debug` (`MTL_SHADER_VALIDATION_TEXTURE_USAGE=0`). The rest of Metal's validation stays on, `macos-clang` on macOS 15 keeps the check, `test_metal_native` checks on both images that every texture the library makes has the usage flags its accesses need, and the cell takes the check back by itself once `test/metal_usage_check.m` prints `ok` there ([#9](https://github.com/Rawframe-Project/maul-rhi/issues/9)).

A red `windows-d3d12-states` run is expected now and then: about 1 or 2 nightly runs in 100 report debug layer message 1358, "Incompatible texture barrier layout", which is the upstream fault of [#7](https://github.com/Rawframe-Project/maul-rhi/issues/7), not a regression. Add that run to #7 and move on; a red run with any other message is a real failure.

A failed cell uploads its ctest logs for a week. The other workflows: `codeql.yml` (CodeQL, built with GCC 14), `pages.yml` (the documentation site), `pr-title.yml` (conventional titles with the scopes of [AGENTS.md](../../AGENTS.md#pull-requests)), `pr-gate.yml`, `release.yml` ([release.md](release.md)) and `scorecard.yml`.

## The rules

| Check | Tool |
|---|---|
| Formatting | `clang-format` 22.1.5, `.clang-format` |
| Naming, complexity | `clang-tidy-20`, `.clang-tidy` |
| SPDX lines, comments, banned calls, file-scope state, file names | `tools/check_source.py` |
| The module graph | `tools/check_modules.py`, `tools/modules.txt` |
| File and function lengths | `tools/check_lengths.py`, `tools/length-exceptions.txt` |
| Public documentation blocks and thread safety statements | `tools/check_docs.py` |
| Documentation snippets run by `test/test_guide.c` | `tools/check_guide.py` |
| Headers and generated sources match the contract | `tools/gen_contract.py --check` |
| Allowed Markdown files, em dash, record numbers, links | the organization's `docs_check.py` |

Each tool's header comment and message state its rule and why.

## What the test presets expect

The Linux presets run Vulkan on lavapipe and set, through their test presets:

| Variable | Effect |
|---|---|
| `MAUL_RHI_REQUIRE_VULKAN` | A test that finds no Vulkan adapter fails instead of skipping. |
| `MAUL_RHI_REQUIRE_SURFACE` | The surface cases and window samples fail without a window system; run under `xvfb-run -a -s "-screen 0 1280x1024x24 -noreset"`. Without `-noreset`, Xvfb resets each time its last client leaves and refuses the clients that connect meanwhile. With `DISPLAY` set, a test whose connection the server refuses fails with xcb's error; only an unset `DISPLAY` is a skip. |
| `MAUL_RHI_REQUIRE_BINDLESS`, `MAUL_RHI_REQUIRE_STATISTICS`, `MAUL_RHI_REQUIRE_TIMESTAMPS` | The heap, pipeline statistics and timestamp cases fail when the adapter does not grant them. The D3D12 preset sets the last two as well. Elsewhere, a conformance case or part left out for a feature the adapter lacks prints `case <name>: skipped without <feature>` and counts in the report, so a skip never reads as a pass. |
| `MAUL_RHI_REQUIRE_EXTERNAL` | `test_conformance_external` must exist and pass. |
| `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`, `VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true` | The validation layer with synchronization validation; any "Validation Error" in a test's output fails it. |
| `LVP_POISON_MEMORY=true` | Lavapipe fills new memory with nonzero bytes, so a read of memory nothing wrote fails reliably. |

The other presets set their platform's counterparts: `MAUL_RHI_REQUIRE_METAL` with `MTL_DEBUG_LAYER` and `MTL_SHADER_VALIDATION`, `MAUL_RHI_REQUIRE_D3D12` with `MAUL_RHI_D3D12_DEBUG` for the debug layer and GPU-based validation (and `MAUL_RHI_D3D12_GBV_STATES=0` without its state tracking), `MAUL_RHI_REQUIRE_WEBGPU` for headless Chrome. A test that finds no adapter and is not required exits 77 and counts as skipped; CI fails any cell with a skipped test, so a preset excludes by name the tests its runner cannot run, with the reason in its description.

## Labels

Every suite `test/test_<name>.c` carries the label `<name>`. Tests that drive a real GPU, window or browser also carry `integration`: the conformance suite and its external and worker forms, the samples (`sample`), the guide, the seam check, the native interop suites and the outside-driver build. `ctest -L <label>` runs one suite, `-LE integration` everything that needs no GPU. In Linux shared builds, `exports` (`tools/check_exports.py`) checks that the library exports every function the public headers declare and nothing else, no data included.
