# Shaders, pipelines and binding

Maul RHI compiles and translates no shaders at run time. Shaders are cooked offline into containers; a device loads a container, makes pipelines from it, and a pass binds resources through binding tables, a small root block of constants and, where the GPU allows, bindless heaps.

## Shader containers

A container holds a set of entry points in every form a driver needs: SPIR-V for Vulkan, WGSL for WebGPU, DXIL for Direct3D 12, and Metal Shading Language or a metallib for Metal, beside one reflection that describes every entry point, binding and constant in WebGPU's terms. The reflection lets the library check and lay out pipelines identically on every API, and refuse any pipeline WebGPU would refuse. [The format](shader-container-format.md) is documented byte by byte for cooks of other kinds.

A build that targets only some drivers needs only their code: a container without WGSL loads everywhere but WebGPU, and one without DXIL everywhere but Direct3D 12.

### Cooking a container

The library's tools make containers from GLSL, HLSL or any source that compiles to SPIR-V:

1. Compile each stage to SPIR-V for Vulkan 1.3 and link them into one module (`glslangValidator`, `spirv-link`, `spirv-val`).
2. Write the reflection as JSON: the root block's size, each entry's stage, inputs, outputs and inter-stage variables, each binding's table, slot and kind, and the specialization constants. The opening comment of `tools/mrhi_container.py` shows the form; enum names are the API's without their prefixes.
3. Make the native code: `tools/mrhi_msl.py SPIRV REFLECTION DIR` writes each entry's MSL through SPIRV-Cross, and `tools/mrhi_dxil.py SPIRV REFLECTION DIR` each entry's DXIL through SPIRV-Cross and DXC.
4. Write the container: `tools/mrhi_container.py [--msl DIR] [--metallib FILE] [--dxil DIR] SPIRV WGSL REFLECTION OUTPUT`, with `-` for the WGSL of a container whose entries read heaps or the view index, or whose SPIR-V uses 64-bit integers, none of which WGSL has.

The writer applies the reader's rules and refuses code that disagrees with the reflection: each module must hold exactly the reflection's entry points, and every binding a module declares must be in the reflection with the same kind. A module may leave out a binding it does not use. `tools/gen_test_shaders.py` runs the whole cook for the library's own tests and samples, and is a working example.

### Loading

`mrhiCreateShader` takes the container's bytes, 8-byte aligned, and checks every one as hostile input, so a damaged or hostile file is refused with `mrhi_errorFormat`, never read past its end. Bad bytes count no misuse, since a correct program can load them from content; NULL or misaligned bytes are `mrhi_errorInvalid` and count one. It also refuses, as unsupported, a container that needs what the device lacks: limits, 16-bit floats, 64-bit integers, subgroups, multiview or heaps. `mrhiGetShaderInfo` reads the reflection back. The bytes may be freed once the call returns.

## Pipelines

`mrhiCreateGraphicsPipeline` and `mrhiCreateComputePipeline` return a pipeline id at once, and `mrhi_devicePipelineReady` answers in the device's queue when it can be used: compiling a pipeline takes long enough to stall a frame, and the browser offers only asynchronous creation. A program starts its pipelines while loading and polls for them as it polls for frames. Destroying a pending pipeline answers it `mrhi_errorStale` at once.

- A graphics def carries every state of WebGPU's render pipeline: vertex buffers and attributes as arrays with counts, primitive state, depth and stencil (with `mrhi_formatNone` for none), multisampling and each color target's format, blending and write mask. A field for an aspect the depth format lacks must keep its default, so a forgotten format is refused rather than ignored.
- Specialization constants are doubles, as in WebGPU, checked against each constant's type. Every constant without a default must be set, by every pipeline made from the container. An entry, a constant id or a value the reflection does not take is `mrhi_errorFormat`, counting no misuse; a NULL entry, NULL values with a count or a constant set twice is `mrhi_errorInvalid`.
- A pipeline's binding layout is its whole container's, and it keeps working after its shader is destroyed.
- Everything the reflection allows is checked before the driver sees the pipeline: a def that contradicts itself or WebGPU's rules is `mrhi_errorInvalid`; state its shader refuses (a vertex input without its attribute, a target or blend the fragment output does not fit, a fragment input no vertex output matches) is `mrhi_errorFormat`, counting no misuse, since a correct program may pair its own state with a shader from content; one the device cannot do is `mrhi_errorUnsupported`, and one past the device's limits [is capacity or unsupported](devices.md#past-a-device-limit). When a def has several faults, invalid input outranks format, format outranks unsupported, and unsupported outranks capacity, so a def with a real bug still counts it.

**Pipeline caches.** `mrhiGetPipelineCache` writes the driver's compiled pipelines in an envelope that names the ABI version, the driver and the adapter. A later device's def may give the bytes back (`pipelineCache`); they are checked as hostile input and never fail the device. `mrhiGetPipelineCacheOutcome` says whether the driver took the cache, or why not: another ABI version (`mrhi_errorVersion`), another driver or adapter (`mrhi_errorStale`), or damage (`mrhi_errorFormat`); [devices.md](devices.md#opening-a-device) says what survives an upgrade. Only the Vulkan driver fills a cache; Direct3D 12, Metal and WebGPU write an empty one, take back only that, and report any other as stale. Metal and the browser keep caches of their own.

## Binding tables and the root block

A pipeline reads its resources through up to four binding tables, set per pass with `mrhiSetBindings`, and through the root block, a few bytes of constants set with `mrhiSetRootBlock` (the limit `rootBlockBytes`, 64 bytes on every adapter and more where one grants it).

- `mrhiSetBindings` sets one whole table: exactly the slots the pass's pipeline's reflection lists for it, at most `MRHI_MAX_TABLE_BINDINGS`. A binding is a buffer range, a texture of the frame seen through an inline view, or a sampler, each checked as WebGPU checks a bind group entry.
- Every bound resource needs an access the pass declares, of the kind the slot needs, so the frame's barriers always cover it.
- A draw or dispatch needs every table its pipeline reads to be set, under a container with the same digest; identical containers are compatible.
- Each pass starts with its root block zeroed.

## Bindless heaps

Where the device grants `bindlessSampling`, a heap (`mrhiCreateHeap`, `maul-rhi/heap.h`) holds resource and sampler entries at indices the program chooses, so a shader can pick resources by index, for instance per material. `bindlessHeterogeneous` adds storage textures and storage buffers to sampled textures. Uniform buffers never go in a heap.

- `mrhiSetHeapEntry` writes an empty entry, and `mrhiSetHeapSampler` an empty sampler entry. `mrhiClearHeapEntry` and `mrhiClearHeapSampler` free an index once the frames submitted before the clear have finished; writing it again earlier, or writing an occupied entry, is `mrhi_errorState`. No running frame ever reads an entry that changes.
- Destroying a view, texture, buffer or sampler empties the entries that name it.
- A pass names at most one heap (`mrhiPassDef::heap`), which all its draws and dispatches read; a pipeline whose shader reads a heap is set only in such a pass.
- The pass still declares every resource it reaches through the heap, so the frame's barriers cover it. Reading an entry the pass neither declares nor has sealed, writing one it does not declare as written, reading an empty entry, or reading an entry as another kind gives undefined results, as the native APIs cannot hide entries.

**Sealing.** Streamed assets are read by many passes and written once. `mrhiSealResource`, on a texture or buffer the open frame imports, leaves it in the sealed state after the frame. From the next frame on, any pass may read it through a heap without declaring it, and a pass that does declare it may only read it, with no barrier. `mrhiUnsealResource` returns it to the frame graph's tracking. Sealing needs `bindlessSampling`.
