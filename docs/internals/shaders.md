# Shader containers and pipelines

Why the library loads containers made offline instead of compiling shaders, how the container is read, and how pipelines are made from it. The byte layout is the user page [shader-container-format.md](../user/shader-container-format.md).

## One container, every code

Vulkan takes SPIR-V, D3D12 DXIL, Metal a metallib or MSL it compiles itself, and the WebGPU driver takes WGSL. The library compiles and translates nothing at run time: a shader compiler would dwarf the library, cost the web build its size budget, and put an untrusted-input parser of enormous surface into every program. So a cook makes every form offline, once, and a container carries them side by side. Every build carries every code, which costs size on disk, not time.

The MSL exists beside the metallib because only Apple's toolchain makes a metallib: with MSL, a cook on Linux or Windows can still target Metal, while the library still translates nothing. The driver prefers the metallib when there is one. DXIL is always made offline, since D3D12 has no compiler of its own; SPIRV-Cross writes HLSL from the SPIR-V and DXC compiles it (`tools/mrhi_dxil.py`).

## One reflection, in WebGPU's terms

Pipelines need each entry point's interface and every binding's kind to make their layouts, and WebGPU needs them explicitly. The container's single reflection is written in WebGPU's bind group layout terms, because WebGPU is the strictest consumer and the other APIs derive their layouts from the same facts. It holds every fact WebGPU's pipeline validation reads from a shader (entry points with their stages, workgroup sizes, workgroup storage, interface variables and builtins; bindings with their tables, slots, stages and kind details; the root block's size; the specialization constants and their defaults), so the core can refuse any pipeline WebGPU would, on every driver. It is also what lets a pipeline be laid out identically on every API.

The native maps (Metal's indices, D3D12's registers and spaces) record where the cook placed each binding rather than imposing a rule, so a cook other than the library's may place them its own way; the reader checks the indices against each API's ranges and each other.

## Reading hostile bytes

A program loads shader bytes from disk or the network, so `mrhiCreateShader` checks every byte before keeping anything: bounds, overlaps, enums, reserved zeros, UTF-8 names, unique names, slots, locations and ids, WGSL's interpolation rules and WebGPU's rules for each binding, and, through the SPIR-V capabilities a module declares first, the features its code needs. It reads byte by byte, so nothing depends on the host's byte order, and refuses rather than repairs. The SHA-256 digest over everything after it identifies the container for pipeline caches and table compatibility, and catches damage. It is standard, small in C (`src/sha256.c`, from FIPS 180-4), and in Python's standard library, which the writer uses.

The device keeps the decoded reflection in one allocation with a reference count, held by the shader and by each pipeline made from it, so a pipeline keeps working after its shader is destroyed without a copy. The drivers check the code itself when they make their modules; a container without the code its driver needs is refused as unsupported.

Unknown section types are skipped after their bounds are checked, so a later writer can add a section without a new format version. Any change to the bytes of a known section raises the version.

`fuzz_container` fuzzes the reader from every container the tests and samples compile in, and from minimal containers of under a kilobyte, one per feature of the format, which `tools/container_seed.py` writes out with `tools/mrhi_container.py` and the least code each section takes; `test/test_container_writer.py` reads each minimal one with the library. Each seed reaches other parts of the format, such as vertex inputs, bindings of every kind, constants, a second blend source, heap uses, 64-bit integers or multiview, and in the small ones mutations land in the table and records rather than in compiled code. SHA-256 is left out of the coverage instrumentation (`test/fuzz_coverage_ignore.txt`), since hashing every input took most of the run and its comparisons guide nothing; `test/test_sha256.c` checks it by known answers. It sets each input's size field and digest before parsing, so mutations reach the sections and records instead of failing the header, and also parses each input cut short. A container that parses must keep its parts inside its bytes and apart, and decode into a reflection that finds every entry and frees what it took.

## The writer

`tools/mrhi_container.py` uses Python's standard library only and applies the reader's rules before writing, then refuses code that disagrees with the reflection: each module must hold exactly the reflection's entry points with their stages, a WGSL compute entry's workgroup size must be literal (no override may change the reflection's) and match, and every binding a module declares must be in the reflection with the same kind, dimension, depth, multisampling and comparison. For DXIL it reads each entry's resources from the `PSV0` part and refuses one outside the map. `test/test_container_writer.py` runs the writer against the reader.

`tools/mrhi_dxil.py` refuses HLSL that reads a loop's variable after the loop's continue block stepped it, which SPIRV-Cross writes for some loops and which would read the next iteration's element; `test/test_dxil_order.py` keeps that check honest.

## Pipelines

Every API compiles pipelines, and compiling takes long enough to stall a frame. WebGPU offers only asynchronous creation, Metal completion handlers, and Vulkan and D3D12 compile on the calling thread unless the host makes threads of its own. The library has no threads and takes no callbacks, so a pipeline is a request: `mrhiCreateComputePipeline` and `mrhiCreateGraphicsPipeline` return the pipeline and a request at once, and the device's queue answers `mrhi_devicePipelineReady`. A creation reserves room for its answer, as a submission does. A driver that compiles synchronously answers at the next poll.

- **Typed ids** for compute and graphics pipelines share one table and one `pipelines` limit; a slot knows its kind, so an id of the other kind is stale.
- **Constants** are doubles, as in WebGPU, checked against each constant's type: a boolean 0 or 1, an integer within its type's range, a finite float within 32-bit range. Every constant without a default is set, each at most once. A container does not record which entry uses which constant, so its required constants are required by every pipeline made from it.
- **Graphics defs** carry every state of WebGPU's render pipeline descriptor, vertex buffers and attributes as arrays with counts so that no fixed array caps an adapter's limits. Depth and stencil state sits in the def with `mrhi_formatNone` for none, and a field for an aspect the format lacks must keep its default, so a forgotten format is refused rather than ignored. Sample counts are 1 or a power of two every target format takes on the device.
- **Checks.** Everything the reflection allows to be checked is checked before the driver sees the pipeline, by WebGPU's rules for compute and render pipelines: a def that contradicts itself, its shader or those rules is invalid, and one the device cannot do is unsupported.

## Pipeline caches

`mrhiGetPipelineCache` writes the driver's compiled pipelines in an envelope: a magic, a version, the size, a SHA-256 digest, and the library version, driver kind and adapter vendor and device ids. A device def may give one back. It is checked as hostile input, then offered to the driver, and never fails the device; `mrhiGetPipelineCacheOutcome` reports it taken, absent, stale (another library version, driver or adapter, or declined by the driver) or damaged. The Vulkan driver keeps one `VkPipelineCache` and takes a blob only when Vulkan's own header names the device and its cache UUID. The other drivers write an empty cache and take back only that: Metal and the browser keep caches of their own, and the D3D12 driver has none yet. `fuzz_cache` fuzzes the envelope.
