# The Maul RHI guide

This guide walks through Maul RHI part by part: how a program finds a
GPU and opens a device, how work reaches it through frames, shaders and
pipelines, binding, presenting to windows, and the per-API doors for
OpenXR and vendor upscalers. The [API reference](api.md) lists every
public function; the [contract](contract/mrhi.json) and its
[mappings](contract/mappings.md) say how each concept maps onto Vulkan,
Direct3D 12, Metal and WebGPU; the design records in
[adr/](adr/mrhi.md) give the reasons.

## 1. The model

A program opens an instance, asks it for adapters, opens a device on
one, and sends work to the device as frames:

```c
#include "maul-rhi/device.h"
#include "maul-rhi/instance.h"

mrhiInstanceDef instanceDef = mrhiDefaultInstanceDef();
mrhiInstance* instance = NULL;
mrhiCreateInstance(&instanceDef, &instance);

mrhiAdapterRequestDef request = mrhiDefaultAdapterRequestDef();
mrhiRequestId searched;
mrhiRequestAdapters(instance, &request, &searched);
mrhiInstanceNotification record;
while (mrhiNextInstanceNotification(instance, &record) != mrhi_success)
{
    // On the web the browser answers between the page's tasks.
}
mrhiAdapterId adapter;
size_t count = 0;
mrhiGetAdapters(instance, &adapter, 1, &count);

mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
deviceDef.adapter = adapter;
mrhiDevice* device = NULL;
mrhiRequestId opened;
mrhiCreateDevice(instance, &deviceDef, &device, &opened);
// mrhi_instanceDeviceReady answers the opening.
```

Three rules hold everywhere:

- **Requests and notifications.** Whatever completes later (an adapter
  search, a device opening, a pipeline, a frame, a readback) returns a
  request id at once and is answered by exactly one record in its
  owner's queue: the instance's (`mrhiNextInstanceNotification`) or the
  device's (`mrhiNextDeviceNotification`).
- **No threads, no callbacks.** The library starts no thread and never
  calls the program. Native drivers answer at the next poll; a browser
  answers between the page's tasks.
- **Frames are the only way work reaches the GPU.** A frame declares
  its resources and passes up front; the library plans the barriers,
  places transient memory, culls what nothing needs, and records each
  driver's commands at submission.

The build has one native driver: Metal on Apple systems, Direct3D 12 on
Windows (or Vulkan, with `MAUL_RHI_VULKAN_DRIVER=ON` and
`MAUL_RHI_D3D12_DRIVER=OFF`), Vulkan on Linux and Android, WebGPU on
the web. The test driver (`maul-rhi/test.h`) runs without a GPU, for a
program's own tests (section 12).

## 2. Results, ids and defs

Functions return an `mrhiResult`: `mrhi_success`, a few positive
outcomes (`mrhi_empty` for a drained queue, `mrhi_suboptimal` and
`mrhi_occluded` for surfaces, `mrhi_timeout`), or an error:
`mrhi_errorInvalid` for a bad argument, `mrhi_errorStale` for an id
whose object is gone, `mrhi_errorCapacity` past a limit,
`mrhi_errorUnsupported` for what the adapter or driver cannot do,
`mrhi_errorState` for a call in the wrong state, `mrhi_errorVersion`
for an instance def built against another contract version,
`mrhi_errorOutOfDate` for a surface to configure again,
`mrhi_errorPlatform`, `mrhi_errorDeviceLost`. `mrhiResultName` names each. Every
`mrhi_errorInvalid` also counts one misuse (`mrhiGetInstanceMisuse`,
`mrhiGetDeviceMisuse`), which a release build can watch.

Objects are ids with a generation: an id outlives its object
harmlessly, and using it afterwards answers `mrhi_errorStale`. Defs
carry a cookie: build each with its `mrhiDefault...` function and change
fields. A def's `next` takes chained structs; one the library does not
know is refused unless its type marks it a hint.

All memory is taken up front, through the def's allocator, from limits
the def names: the instance's (`mrhiInstanceLimits`) and the device's
(`mrhiDeviceLimits`: objects, frame resources, passes, accesses,
command, upload and readback bytes). Each owner object takes its memory
from its own allocator; a zeroed allocator is the C library's.

## 3. Adapters, features and limits

`mrhiRequestAdapters` searches, software rasterizers allowed when the
request says so; `mrhiGetAdapters` lists the result, best first, and
reports the total, which may exceed the room given.
`mrhiGetAdapterInfo` names an adapter and its driver.

Every adapter meets a floor: `mrhiDefaultLimits` and the features every
driver has. Above it, optional features (`mrhiFeatures`: compressed
format families, 16-bit floats, bindless heaps, unclipped depth and
more) and raised limits are read with `mrhiGetAdapterFeatures` and
`mrhiGetAdapterLimits`, and asked for in the device def: a device has
exactly what it asked for, so a program that runs on one adapter runs
on any that grants the same. `mrhiGetFormatCaps` says what a format can
do: sampling, filtering, rendering, blending, storage, sample counts.

## 4. Devices

`mrhiCreateDevice` returns the device at once in the opening state;
`mrhi_instanceDeviceReady` answers when it is ready
(`mrhiGetDeviceState`). A device is lost when its driver loses the GPU:
every later call answers `mrhi_errorDeviceLost`, the queue gives
`mrhi_deviceLostNotice` once, and `mrhiGetDeviceLossReport` says why. A
program recovers by destroying the device and opening another;
`mrhiSimulateDeviceLoss` tests that path.

## 5. Resources

Device objects outlive frames: buffers (`mrhiCreateBuffer`), textures
(`mrhiCreateTexture`) and their views (`mrhiCreateView`), samplers,
query sets. A texture's usages are fixed at creation, and its view
formats may name only its sRGB or linear twin. Destroying an object a
submitted frame still uses is safe: the driver retires it once that
frame has finished.

Frames also declare transient resources (`mrhiDeclareBuffer`,
`mrhiDeclareTexture`), which live for the frame alone: the compile
places them in the frame's memory, overlapping those whose uses do not
overlap, and on tile GPUs keeps a target that is only written and
discarded on chip.

## 6. Frames

A frame is begun, built, compiled, recorded and submitted:

```c
mrhiFrameDef frame = mrhiDefaultFrameDef();
mrhiBeginFrame(device, &frame);

mrhiTextureDef targetDef = mrhiDefaultTextureDef();
targetDef.format = mrhi_formatRgba8Unorm;
targetDef.width = 64;
targetDef.height = 64;
mrhiResourceId target;
mrhiDeclareTexture(device, &targetDef, &target);

mrhiPassDef drawDef = mrhiDefaultPassDef();
drawDef.colorTargets[0] = (mrhiColorTarget){
    .resource = target, .load = mrhi_loadClear, .store = mrhi_storeKeep};
drawDef.colorTargetCount = 1;
mrhiPassId draw;
mrhiAddPass(device, &drawDef, &draw);

const mrhiAccess read = {.resource = target, .kind = mrhi_accessCopySource,
                         .range = {.mipCount = 1, .layerCount = 1}};
mrhiPassDef readDef = mrhiDefaultPassDef();
readDef.passClass = mrhi_passTransfer;
readDef.accesses = &read;
readDef.accessCount = 1;
readDef.neverCull = true;
mrhiPassId reading;
mrhiAddPass(device, &readDef, &reading);

mrhiCompileFrame(device);

mrhiBeginPass(device, draw);
mrhiSetGraphicsPipeline(device, draw, pipeline);
mrhiDraw(device, draw, 3, 1, 0, 0);
mrhiEndPass(device, draw);

mrhiRequestId pixels;
const mrhiTextureCopy source = {.resource = target};
const mrhiExtent3d extent = {64, 64, 1};
mrhiBeginPass(device, reading);
mrhiReadTexture(device, reading, &source, &extent, &pixels);
mrhiEndPass(device, reading);

mrhiRequestId token;
mrhiSubmitFrame(device, &token);
```

- **Building.** Each pass names what it touches: its accesses
  (`mrhiAccess`: a resource, how it is used, which mips and layers) and
  its targets. Device objects join a frame through `mrhiImportBuffer`
  and `mrhiImportTexture`, and carry their state from frame to frame.
- **Compiling.** `mrhiCompileFrame` culls passes whose results nothing
  needs (unless `neverCull`), orders barriers, chooses each target's
  store, and places transients. `mrhiIsPassKept`, `mrhiGetPassPlan`,
  `mrhiGetResourcePlan`, `mrhiGetFrameBarriers` and
  `mrhiGetFrameMemory` show the result.
- **Recording.** Between `mrhiBeginPass` and `mrhiEndPass` the encoder
  calls record a pass's commands: pipelines, bindings, the root block,
  dynamic state, draws, dispatches, copies, debug groups, queries.
  Passes may be recorded from several threads at once, each pass by one.
  An encoder call that breaks WebGPU's rules is refused on every
  driver, so a frame that records anywhere records on the web.
- **Uploads and readbacks.** `mrhiWriteBuffer` and `mrhiWriteTexture`
  copy the program's bytes at the call into the frame's staging;
  `mrhiReadBuffer` and `mrhiReadTexture` answer with
  `mrhi_deviceReadbackReady` once the frame has finished, and
  `mrhiTakeReadback` copies the bytes out.
- **Clears.** `mrhiClearBuffer` zeros a range of a buffer in a pass
  that declares it a copy destination, without staging: counters,
  indirect arguments and a culling pass's output start from zero this
  way.
- **Counted draws.** Where the device has `multiDrawIndirectCount`,
  `mrhiDrawIndirectCount` and `mrhiDrawIndexedIndirectCount` make up to
  `maxCount` indirect draws from packed records, the number drawn read
  on the GPU from a count a culling pass wrote. A frame's `maxCount`s
  together stay within the device limit `frameIndirectDraws`.
- **Multiview.** Where the device has `multiview`, a pass's `viewCount`
  renders it into that many layers of its targets, from each target's
  layer, with pipelines made for as many views; shader entries read
  the view index (`mrhi_builtinViewIndex`), so their containers carry
  no WGSL. The adapter limit `multiviewViews` bounds the views.
- **Submitting.** `mrhiSubmitFrame` returns a token that
  `mrhi_deviceFrameDone` answers; `mrhiWaitFrame` blocks for it, with a
  timeout, where the platform allows blocking. `mrhiDropFrame` abandons
  an open frame.

## 7. Shaders and pipelines

Shaders are compiled offline into containers: SPIR-V and WGSL for each
entry point, with DXIL and Metal Shading Language beside them where a
build needs them, and one reflection in WebGPU's binding terms
(`docs/contract/container.md`; `tools/mrhi_container.py` writes one).
`mrhiCreateShader` loads a container's bytes, checked as hostile input,
and `mrhiGetShaderInfo` reads its reflection.

`mrhiCreateGraphicsPipeline` and `mrhiCreateComputePipeline` return a
pipeline id at once; `mrhi_devicePipelineReady` answers when it can be
used, since creation stalls a browser's page when it is synchronous. A
pipeline's binding layout is its container's reflection; it keeps
working after its shader is destroyed. A device def may carry a
pipeline cache blob from an earlier run, which native drivers import
(`mrhiGetPipelineCacheOutcome`) and `mrhiGetPipelineCache` exports.

## 8. Binding

A pipeline reads its resources through binding tables, set per pass
with `mrhiSetBindings`, and through a small root block of constants
(`mrhiSetRootBlock`). Every binding names a resource of the frame, so
the compile sees it.

Where the adapter grants `bindlessSampling`, bindless heaps
(`mrhiCreateHeap`, `maul-rhi/heap.h`) hold resource and sampler entries
at indices the program chooses; a pass names one heap and sees the
entries of the resources it declares. A resource sealed for reading
(`mrhiSealResource`) is visible to every pass without being declared,
until it is unsealed.

## 9. Surfaces and presenting

A surface is a window's drawable area, made from its native handles
with one chained source struct per window system (Win32, Wayland, XCB,
Android, a CAMetalLayer, a canvas) by `mrhiCreateSurface`; the library
includes no window system header. `mrhiGetSurfaceCaps` says what an
adapter can do with it: formats, color spaces (primaries, transfer and
range as separate fields), present modes, sizes.
`mrhiSuggestSurfaceColor` picks a color in the library's fallback
order, and `mrhiConfigureSurface` configures it on a device.

In a frame, `mrhiAcquireSurfaceImage` gives the next image as a frame
resource that begins undefined and is presented when the frame is
submitted. It may answer `mrhi_occluded` (no image while the window is
hidden or empty) or `mrhi_errorOutOfDate` (configure again first). A
device presents to many surfaces, each with its own present mode.

## 10. Queries and timing

Query sets hold occlusion results, timestamps or pipeline statistics;
a pass names the occlusion and timestamp sets it writes, statistics
queries name theirs as they begin (`mrhiBeginStatisticsQuery`, on
Vulkan and Direct3D 12), and `mrhiResolveQueries` writes results into
a buffer.
`mrhiGetDeviceTimestampPeriod` converts timestamps to nanoseconds.
Present timing is a capability no driver grants yet.

## 11. Native doors

The portable API holds no native handle. Where a program needs one, a
per-API header gives it, and every other driver answers its functions
`mrhi_errorUnsupported`.

- **OpenXR (`maul-rhi/vulkan.h`).** An instance made by the runtime is
  adopted (`mrhiInstanceVulkanAdopt`), with the extensions it was made
  with; `mrhiDescribeVulkanDevice` gives the `VkDeviceCreateInfo` the
  runtime makes the device from, which a device def then adopts
  (`mrhiDeviceVulkanAdopt`). Swapchain images are adopted as textures
  (`mrhiTextureVulkanAdopt`), and every frame leaves them in the layout
  OpenXR takes them back in. `mrhiGetVulkanPhysicalDevice` and
  `mrhiGetVulkanQueue` give what the session binds. Adopted objects are
  never destroyed by the library.
- **Vendor upscalers.** A native pass (`mrhiPassDef::native`) declares
  its accesses to imported resources and is recorded by the program in
  a command buffer of its own, handed over with
  `mrhiSetVulkanPassCommands`, `mrhiSetD3d12PassCommands` or
  `mrhiSetMetalPassCommands`; the library runs it between its own, with
  every declared resource in its access's layout or state.
  `mrhiGetVulkanDevice`, `mrhiGetD3d12Device` and `mrhiGetMetalDevice`
  give the native device, and `mrhiGetVulkanTexture`,
  `mrhiGetD3d12Texture` and `mrhiGetMetalTexture` a texture's native
  object, as FSR, DLSS, XeSS and MetalFX take them. A Vulkan device def
  may add the extensions and core feature structs an upscaler requires
  (`mrhiDeviceVulkanExtensions`).

## 12. Building and testing

Requirements: CMake 3.25 and GCC 14 or Clang 19 or newer; on Windows,
`clang-cl`. The NDK builds Android
(`-DCMAKE_TOOLCHAIN_FILE=cmake/android-emulator.cmake`), Emscripten or
Clang's `wasm32-wasi` the web
(`-DCMAKE_TOOLCHAIN_FILE=cmake/wasm32-wasi.cmake`), and the iOS
simulator has its own toolchain file. The conformance suite
(`test/test_conformance.c`) runs every check on the build's native
driver and on the test driver; the samples in `samples/` check their
own results; `maul-rhi_bench` prints the CPU cost per draw and pass, a
compile's time and the bytes of an instance and a device, with their
ratio to `bench/baseline.txt`.

A program's own tests can run on the test driver, built when
`MAUL_RHI_TEST_DRIVER` is on (as it is with the library's tests): chain
an `mrhiTestDriverDef` on the instance def, with the adapters it should
find. Every call is checked, and every request answered, as on a GPU,
but nothing runs: frames finish at once and readbacks hold no rendered
bytes.
