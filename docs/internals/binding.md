# Binding

How shaders reach resources: binding tables and the root block on every API, bindless heaps where the API has them, and sealed resources that heaps read without barriers. The constraint throughout is that the frame graph must still know every resource each pass touches, since it alone makes the barriers.

## Binding tables

A pipeline's layout comes from its container's reflection: up to four tables (WebGPU's `maxBindGroups`), each a set of slots of fixed kinds. `mrhiSetBindings` sets one whole table: exactly the slots the reflection lists for it, under the pipeline set in the pass, which is looked up by id again on each call. A binding is a buffer range, a texture of the frame seen through an inline view, or a sampler, each checked as WebGPU checks a bind group entry: alignment, size limits and minimum sizes; view kind, format, sample type and multisampling; sampler kind. Each bound resource also needs a declared access of the pass covering it, of the kind the slot needs.

A table and its command fit one arena chunk, so a table holds at most `MRHI_MAX_TABLE_BINDINGS`. Draws need every table their pipeline reads, set under a container of the same SHA-256 digest: identical containers made apart stay compatible, which matters when two pipelines of one material come from separately loaded files.

On each API a table becomes what it binds cheaply: a Vulkan descriptor set taken from the frame slot's pools, a pair of D3D12 descriptor tables written into the slot's shader-visible rings, Metal buffer, texture and sampler indices set before each draw, a WebGPU bind group made at its command.

## The root block

The root block is a few bytes of constants per pass, set with `mrhiSetRootBlock` at 4-byte granularity within `rootBlockBytes`: 64 bytes on the floor, raised where every device of an API allows more (128 on D3D12, inside the root signature's 64 words). It is a Vulkan push constant range for the pipeline's stages, D3D12 root constants, a Metal buffer at the index the container's map gives, and WebGPU immediates. Each pass starts with it zeroed, which each driver ensures; the core does not track what a draw reads of it.

## Bindless heaps

A renderer that streams thousands of materials wants shaders to pick resources by index. D3D12 and Vulkan keep resources and samplers in separate shader-visible heaps, Metal reads argument buffers, and the gpuweb bindless proposal adds one resource table per pass with per-resource usages and slots freed only after the work that saw them. Two features cover the tiers the APIs share: `bindlessSampling` (sampled textures and samplers) and `bindlessHeterogeneous` (storage textures and storage buffers too), with the heap sizes as limits.

- A heap is a device object with a resource part and a sampler part, sized within the device's heap limits, at most `heaps` of them (4 by default).
- **Entries stay where the program puts them.** Writing an occupied entry is `mrhi_errorState`. Clearing an entry frees its index only once the frames submitted before the clear have finished, so no running frame reads an entry that changes, and every API can write entries in place without copies. Destroying a view, texture, buffer or sampler empties the entries naming it.
- **One heap per pass.** A pass names at most one, which every draw and dispatch of the pass reads, as the gpuweb proposal requires a table for such a pipeline's draws.
- **Declared access stays the rule.** A pass declares every resource it reaches through the heap, as it declares bound ones, so the graph makes its barriers. The native APIs cannot hide entries a pass did not declare, as the proposal's tables will, so reading such an entry, or one of the wrong kind or empty, has undefined results instead of an error.
- **Reflection.** Each entry point records what it reads through the heap (`mrhiShaderHeapUses`). SPIR-V reads the resource heap at set 4 binding 0 and the sampler heap at set 4 binding 1, where DXC places Shader Model 6.6 heaps with `-fvk-bind-resource-heap 0 4 -fvk-bind-sampler-heap 1 4`, so existing HLSL compiles unchanged. WGSL reads no heaps until WebGPU's resource tables ship, so a container that uses a heap has no WGSL.
- **Drivers** get a heap handle per pass and write entries as the core accepts them. They also get, per kept pass, the resources it declares with the states they leave, and per frame resource whether it began the frame sealed, for drivers that track buffer states themselves.

How each API provides heaps is on its page: Vulkan through descriptor indexing and mutable descriptors ([vulkan.md](vulkan.md#heaps)), D3D12 on resource binding tier 3 ([d3d12.md](d3d12.md#heaps)). Metal grants heaps only on `MTLGPUFamilyMetal3` devices, whose descriptors fill a plain buffer per heap, which SPIRV-Cross writes from MSL 3.0; no CI device has that family yet, so the Metal driver grants none. WebGPU never does.

## Sealing

Streamed assets are written once and read by many passes, and declaring each of them in each pass that might sample it through a heap defeats bindless. Sealing is part of a frame: `mrhiSealResource`, on a texture or buffer the open frame imports, ends it in `mrhi_stateSealed` after the frame's passes, as a surface image ends ready to present, so the frame that uploads a resource is the one that seals it. From the next frame on, any pass may read it through a heap without declaring it, and a frame that imports it may declare only the reads the sealed state allows: a texture sampled; a buffer as uniforms, vertices, indices, indirect arguments or storage read. Those reads leave it sealed, so they need no barrier. `mrhiUnsealResource` returns it to the graph's tracking for the passes after it; whichever of the two calls comes last in a frame decides how it ends.

On Vulkan the sealed state is `SHADER_READ_ONLY_OPTIMAL` with every shader stage's sampled reads for a texture, and every read for a buffer. On D3D12 a sealed buffer starts each frame in every read state the sealed state allows. Sealing needs `bindlessSampling`.
