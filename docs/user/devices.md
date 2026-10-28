# Adapters and devices

An adapter is a GPU a driver found; a device is a program's connection to one. This page covers finding adapters, what they offer, opening a device that asks for exactly what the program needs, and what happens when the GPU is lost.

## Finding adapters

`mrhiRequestAdapters` searches with a power preference, software rasterizers allowed when the request says so, and the instance's queue answers it with `mrhi_instanceAdaptersFound`. `mrhiGetAdapters` then lists the adapters best first and reports the total, which may exceed the room given. `mrhiGetAdapterInfo` names an adapter and its driver.

A request may name a surface (`compatibleSurface`): only adapters that present to it are listed, and the search answers `mrhi_errorStale` if the surface ends first. An adapter found again by a later search keeps its id.

## Features, limits and formats

Every listed adapter meets a floor, which a program can rely on without asking: compute with storage buffers, storage textures and atomics, indirect draws and dispatches, instancing, 2D, 2D array, cube, cube array and 3D textures, independent blending, occlusion queries, and the limits `mrhiDefaultLimits` returns, which are WebGPU's defaults. An adapter below the floor is not listed.

Above the floor, each adapter reports:

- **Optional features** (`mrhiGetAdapterFeatures`, `mrhiFeatures`): timestamps, pipeline statistics, the BC, ETC2 and ASTC compressed formats, filterable 32-bit floats, rendering to `rg11b10ufloat`, dual-source blending, unclipped depth, 16-bit floats, subgroups and 64-bit integers in shaders, indirect draws with a first instance, counted multi-draw indirect, multiview, bindless heaps (`bindlessSampling`, `bindlessHeterogeneous`) and present timing.
- **Limits** (`mrhiGetAdapterLimits`, `mrhiLimits`): texture sizes, binding counts per table and stage, buffer sizes and alignments, workgroup sizes, heap sizes, the root block's bytes, the views of a multiview pass and more. Alignments are powers of two and better lower; every other limit is better higher.
- **Format capabilities** (`mrhiGetFormatCaps`): whether a format samples, filters, renders, blends and serves as storage, and at which sample counts.

A feature is granted only where the driver's API can do it and the adapter reports it. The [mappings](https://rawframe-project.github.io/maul-rhi/mappings.html) page of the documentation site shows, for every feature, limit and format, how each of Vulkan, Direct3D 12, Metal and WebGPU provides it, and which do not. Present timing is reported by no driver yet; on Metal, neither are timestamps nor bindless heaps.

## Opening a device

`mrhiCreateDevice` takes a def naming the adapter, the features and the limits the program asks for (the floor by default, `framesInFlight` among them), its own budgets (`deviceLimits`), its allocator, and optionally a pipeline cache from an earlier run. It returns the device at once, opening; the instance's queue answers with `mrhi_instanceDeviceReady` when it is ready or failed (`mrhiGetDeviceState`).

A pipeline cache is state, not an asset: it is read only under the ABI version that wrote it (`MRHI_ABI_VERSION`). Before 1.0 that is the whole version, so every other release refuses the cache, a patch release included. From 1.0 on it is the major and minor version, so a patch release keeps the cache and a minor release refuses it. One of another ABI version or envelope version is refused with `mrhi_errorVersion`, and one from another driver or adapter with `mrhi_errorStale`; either way the device opens without it, and `mrhiGetPipelineCacheOutcome` reports why. Keep shader containers, not caches, across an upgrade, and write a new cache from the new version.

A device has exactly what it asked for, not what the adapter could give: asking for more than the adapter grants is `mrhi_errorUnsupported`, and a limit below the floor, or an alignment that is not a power of two, is invalid. A program that runs on one adapter therefore runs on any other that grants the same. `mrhiGetDeviceFeatures` and `mrhiGetDeviceLimits` read back what was granted.

### Past a device limit

A call given a value past one of the device's limits answers by what the adapter can do. When the adapter's limit (`mrhiGetAdapterLimits`) holds the value, the call is `mrhi_errorCapacity`: a device asking for more would take it. When the adapter's limit does not hold it either, the call is `mrhi_errorUnsupported`. For an alignment, a value on the adapter's alignment but off the device's is capacity. This holds alike for:

- **Creations:** a buffer's or texture's size, a heap's entries, a shader's root block, bindings, workgroup or interface, a graphics pipeline's vertex buffers, attributes, strides, inter-stage variables, color bytes or views, and a surface's size.
- **Recording calls:** a table index, a vertex buffer slot, a binding's offset or size, a heap entry's offset, a pass's views, the root block, a viewport, and a dispatch.

Neither counts as misuse. A frame call refused as capacity this way is recorded under `mrhi_diagnosticDeviceLimit`.

A device's own budgets, `mrhiDeviceLimits`, size everything it allocates when it is made: the buffers, textures, views, samplers, shaders, pipelines, query sets and heaps it holds at once, a frame's resources, passes, accesses and barriers, and the bytes for recorded commands (`frameCommandBytes`), uploads (`frameUploadBytes`) and readbacks (`readbackBytes`). A call past one answers `mrhi_errorCapacity`. The limit `framesInFlight` bounds the frames submitted and not yet finished; past it, beginning a frame is refused for capacity rather than blocking.

To size a frame's budgets from what frames take, read `mrhiGetFrameUsage` after a frame compiles or is submitted. It holds what the frame begun last took of each frame limit, each member named after the `mrhiDeviceLimits` member it is measured against, so the headroom is the difference. The library's own pass, which zeroes new objects, is left out. After a refusal it holds what the frame took before it, until the next frame begins. With a `diagnostics` capacity, each refusal by a frame's limit also records a code naming the limit, such as `mrhi_diagnosticFramePasses` or `mrhi_diagnosticFrameUploadBytes`; it counts no misuse.

An instance refuses to be destroyed while devices made on it exist, counting the call as misuse. A device destroyed while it is still opening answers its request with `mrhi_errorStale`.

## Device loss

A GPU can hang, be reset or be removed, and a driver can fail. None of the four APIs recovers a device in place, so a loss is terminal. When a driver reports one:

1. The device's queue gives `mrhi_deviceLostNotice` once, then answers every frame, readback and pipeline still owed with `mrhi_errorDeviceLost`, in order.
2. `mrhiGetDeviceState` answers `mrhi_deviceLost`, and every call that needs the GPU (making objects, beginning a frame, acquiring an image) answers `mrhi_errorDeviceLost`. Submitting drops the open frame. Destroying objects and the device still works.
3. `mrhiGetDeviceLossReport` gives a fixed-size report: the reason (hung, reset, removed, a driver fault, or unknown), the last frame submitted and the last finished, room for the frame and pass the GPU was running, and the driver's message. Direct3D 12 names the reason from the device's removal reason; the other drivers report the reason as unknown, and no driver yet fills in the faulting frame and pass.

The program recovers by destroying the device and opening a new one, then making its objects again. `mrhiSimulateDeviceLoss` loses a ready device on any driver exactly as a driver's report would, so a program can test its recovery; `samples/device_loss.c` does.
