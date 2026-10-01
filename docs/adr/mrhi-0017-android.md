# mrhi-0017. Android: the Vulkan driver in the emulator

Status: Accepted

## Context

The Vulkan driver (mrhi-0003) has always compiled its Android surface
(`VK_KHR_android_surface`, `mrhiSurfaceSourceAndroid`) on `__ANDROID__`,
but no build or test ever ran it there. Android's loader is
`libvulkan.so`, which the driver already tries after `libvulkan.so.1`.

The Android emulator with SwiftShader (`-gpu swiftshader_indirect`)
offers Vulkan 1.3 on its Android 15 image, a CPU device with dynamic
rendering, synchronization2, timeline semaphores, buffer device
addresses and descriptor indexing, but no descriptor heaps or mutable
descriptors. Its Android 11 image's loader stops at Vulkan 1.1, below
the driver's floor.

Run there, the driver found no adapter: two limits were mapped onto
Vulkan wrongly, and both reject many mobile devices as well.

- `bindingsPerTable`, the bound on a slot's number in a table
  (WebGPU's `maxBindingsPerBindGroup`), was read from
  `maxPerStageResources`, which bounds how many resources a stage
  binds, not their numbers; SwiftShader's is 200, under the
  contract's 1000. Vulkan bounds no binding number.
- `tablesPlusVertexBuffers` was the sum of the descriptor sets and
  vertex buffers, 20 on a device with four sets, under the contract's
  24. Vulkan bounds the two apart, never together.

## Decision

- **Build:** `cmake/android-emulator.cmake` takes the NDK's toolchain
  (r28 or newer, x86_64 and Android 11 unless told otherwise) and runs
  the tests on the emulator through `tools/run_android.sh`, which pushes
  each executable over adb and passes the host's `MAUL_RHI_*`
  variables, which adb's shell does not carry. A cross build looks for
  no XCB.
- **Slots:** the Vulkan driver reports the contract's 1000 slots, as the
  other drivers do.
- **Per-stage resources:** the five per-stage limits (sampled textures,
  samplers, storage buffers, storage textures, uniform buffers) are
  what a pipeline may bind at once, so their sum is fitted under
  `maxPerStageResources` less the color attachments, which a fragment
  stage's resources include: the limits above a common cap come down to
  it, the cap the highest that fits, none below the contract's floor,
  whose sum (56, with 8 attachments 64) fits Vulkan's least bound of
  128. Devices whose bound is large keep their limits.
- **Tables and vertex buffers:** their sum is the most a pipeline
  reaches, so the driver reports it, or the contract's 24 when the sum
  is less; a larger value bounds nothing more.
- **CI:** an Android 15 emulator job runs every suite with a Vulkan
  adapter required. Surfaces need a window and are tested on Android
  when an application test hosts one.

## Consequences

Devices with four descriptor sets or a small per-stage bound, common on
Android, now meet the floor. The conformance suite and every sample
that draws, computes or loses its device run on SwiftShader in CI; the
window samples skip there.
