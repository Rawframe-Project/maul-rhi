# Troubleshooting

How to learn why a call was refused, check a driver, see names in GPU debuggers, and test a program without a GPU.

## Why a call was refused

Every `mrhi_errorInvalid` counts one misuse on its instance or device (`mrhiGetInstanceMisuse`, `mrhiGetDeviceMisuse`), which a release build can watch cheaply: a count that grows means the program has a bug. Bad data, a damaged container or an entry, constants or pipeline state its reflection refuses, is `mrhi_errorFormat` and counts none, so data a program does not control cannot move the count.

To learn which check refused a call, give the instance's or the device's limits a `diagnostics` capacity (0 by default, keeping none). Each refusal, `mrhi_errorInvalid` or `mrhi_errorFormat`, is then also a record in a bounded queue, as is each `mrhi_errorCapacity` of a frame call, under a code naming the limit it reached, taken with `mrhiNextInstanceDiagnostic` or `mrhiNextDeviceDiagnostic`. A record holds an `mrhiDiagnosticCode`, which names the check, and a count: a refusal by the same check as the newest record adds to its count. A full queue keeps its first records and drops new ones; the misuse counts stay exact.

`mrhiDiagnosticText` gives each code's English sentence. Compare codes, not texts: texts may be reworded.

Results other than `mrhi_errorInvalid` say what went wrong by themselves; [getting started](getting-started.md#the-rules-every-call-follows) lists them, and each function's documentation says when it returns each.

## The validation layer

The core already checks every call on every build, including every use of a resource against its pass's declared accesses. What the core cannot check is the driver below it. A build with `MAUL_RHI_VALIDATION=ON` adds a layer between the core and the driver that walks every submitted frame and checks every answer the driver gives: events only for requests made, adapters with distinct handles and names inside their buffers, objects made with valid handles, sensible memory alignments and sample counts, device tables that pass the handshake.

Breaches are counted, never printed: `mrhiGetDriverFaults` returns the count of an instance and its devices, always 0 without the layer, and each breach is also a record in the instance's diagnostic queue under a driver code. A driver built outside the library should run its conformance suite this way ([external drivers](external-drivers.md#admission)). The layer costs a wrapper per driver call and a walk per frame, so release builds leave it out.

Errors in shaders, such as reads out of bounds, are the native validators' to find: the Vulkan validation layers, the D3D12 debug layer with GPU-based validation, Metal's API and shader validation (`MTL_DEBUG_LAYER`, `MTL_SHADER_VALIDATION`), and the browser's WebGPU errors. Maul RHI's frames are meant to pass all of them, synchronization validation included; an error they report is a bug worth reporting.

## Names in GPU tools

Objects, passes, debug groups and markers carry labels: a def's `label`, a pass def's label, `mrhiPushDebugGroup`, `mrhiPopDebugGroup` and `mrhiInsertDebugMarker`. They show in RenderDoc, PIX, Xcode and the browser's tools where the API takes them; on Vulkan they need `VK_EXT_debug_utils`, which the instance enables where the loader offers it.

A build with `MAUL_RHI_LABELS=OFF` still checks every label, so a call refused in one build is refused in every build, and then drops it: nothing is named, copied or recorded, and debug groups still have to balance. Every other result is the same.

## Device loss

See [devices](devices.md#device-loss) for what a lost device answers and how to recover. The loss report's reason, frames and message are small enough to keep or send with a crash report.

## Testing a program without a GPU

The test driver, built when `MAUL_RHI_TEST_DRIVER` is on (as it is when the library's tests are), lets a program's own tests run on any machine. Chain an `mrhiTestDriverDef` on the instance def with the adapters it should find, each described by an `mrhiTestAdapter`: its facts, features and limits, how a device opening on it ends, and optionally a flag that loses its devices when set.

Every call is checked and every request answered exactly as on a GPU, but nothing runs: frames finish at once, and readbacks hold no rendered bytes. Each submitted frame is walked as a real driver would read it, and the walk stops the test at any record a driver could not translate. `mrhiTestAdapter.frameLog` points at an `mrhiTestFrameLog` that counts what the walk found (passes, barriers, transients, commands, upload bytes), so a test can check what its frames recorded. Those counts include the library's pass that zeroes new buffers and textures, with one clear per object, in the first frame that imports each.
