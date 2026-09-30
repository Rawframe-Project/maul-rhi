# mrhi-0015. Bindless heaps: one heap per pass, stable entries, sealed reads

Status: Accepted

## Context

Binding tables (mrhi-0011) work on every API, browsers included, but a
renderer that streams thousands of materials wants shaders to pick
resources by index. D3D12 and Vulkan keep resources and samplers in
separate shader-visible heaps, Metal reads argument buffers, and the
gpuweb bindless proposal adds one resource table per pass with
per-resource usages and slots freed only after the work that saw them.
Two features cover the tiers the APIs share: `bindless_sampling`
(sampled textures and samplers) and `bindless_heterogeneous` (storage
textures and buffers too), with the heap sizes as limits (mrhi-0006).
The frame graph must still know what each pass touches to make its
barriers.

## Decision

- **Heaps** are device objects (`mrhiCreateHeap`, `mrhiDestroyHeap`)
  with a resource part and a sampler part, sized within the device's
  `heapSize` and `samplerHeapSize`, under the device limit `heaps`
  (4 by default). They need `bindless_sampling`; a destroyed heap
  retires after the frames that used it.
- **Entries** stay where the program puts them.
  `mrhiSetHeapEntry` writes an empty resource entry: a texture view
  read as a sampled texture, or with `bindless_heterogeneous` a view
  read or written as a storage texture (one mip) or a buffer range as
  a storage buffer (aligned to `storageOffsetAlignment`, a multiple of
  4 bytes). `mrhiSetHeapSampler` writes an empty sampler entry.
  Uniform buffers never go in a heap. Clearing an entry frees its
  index once the frames submitted before the clearing finish; writing
  it earlier, or writing an occupied entry, is `mrhi_errorState`, so
  no running frame reads an entry that changes. Destroying a view, a
  texture, a buffer or a sampler empties the entries naming it.
  Reading an empty entry, or an entry as another kind, is invalid and
  its result undefined.
- **One heap per pass:** a pass def names at most one heap (`heap`),
  which every draw and dispatch of the pass reads.
- **Declared access:** a pass declares every resource it reaches
  through the heap, as it declares bound ones, so the graph makes its
  barriers. Reading an entry of a resource the pass neither declares
  nor has sealed, or writing one it does not declare as written, is
  invalid, with undefined results: the native APIs cannot hide such
  entries, as the gpuweb proposal's tables will.
- **Sealed resources:** sealing is part of a frame.
  `mrhiSealResource` on a texture or buffer the open frame imports
  ends it in the sealed state (`mrhi_stateSealed`) after the frame's
  passes, as a surface image ends ready to present, so the frame that
  uploads a resource seals it. From the next frame on any pass may read
  it through a heap without declaring it, and a frame that imports it
  may declare only the reads the sealed state allows: a texture
  sampled, a buffer as uniforms, vertices, indices, indirect arguments
  or storage read. Those reads leave it sealed, so they need no
  barrier. `mrhiUnsealResource` returns it to the graph's tracking for
  the passes after it; the last of the two calls in a frame decides
  how it ends. On Vulkan the sealed state is `SHADER_READ_ONLY_OPTIMAL`
  with every shader stage's sampled reads for a texture, and every read
  for a buffer. Sealing needs `bindless_sampling`.
- **Shader reflection:** each entry point of a container records what
  it reads through the heap (`mrhiShaderHeapUses`: sampled textures,
  storage textures, storage buffers, samplers, and writes to the
  storage kinds). SPIR-V reads the resource heap at set 4 binding 0 and
  the sampler heap at set 4 binding 1, where DXC places SM 6.6 heaps
  with `-fvk-bind-resource-heap 0 4 -fvk-bind-sampler-heap 1 4`, so
  existing HLSL compiles unchanged. WGSL reads no heaps until WebGPU's
  resource tables ship, so a container using a heap has no WGSL.
  Creating a shader whose entries use heaps needs the features they
  name, and a pipeline whose entry points read a heap is set only in a
  pass that names one, as the gpuweb proposal requires a table for such
  a pipeline's draws.
- **Drivers** get a heap handle per pass and write entries as the core
  accepts them (`createHeap`, `destroyHeap`, `writeHeapEntry`,
  `writeHeapSampler`; SPI version 2). SPI version 3 also gives each
  kept pass the resources it declares, with the states they leave, and
  each frame resource whether it began the frame sealed, for drivers
  that track buffer states themselves (D3D12). The test driver checks
  every handle it is given.
- **Vulkan** grants `bindless_sampling` with descriptor indexing over
  sampled images and samplers (runtime arrays, partially bound and
  update after bind bindings updatable while pending, non-uniform
  indexing) and five bound sets, and `bindless_heterogeneous` with the
  storage types' equivalents, storage images without a format and
  `VK_EXT_mutable_descriptor_type`. The heap sizes are the update after
  bind limits less what four full tables and the color targets may
  take, since per-stage limits count every set. A device has one set
  layout at set 4: binding 0 holds `heapSize` sampled images, or
  mutable descriptors that may also be storage images and buffers, and
  binding 1 `samplerHeapSize` samplers; only a set's last binding may
  vary in count, so every heap takes the device's counts. Each heap is
  a set from its own pool; pipelines from containers reading heaps
  have the layout at set 4 and bind the pass's heap after each
  pipeline, since other table layouts disturb it.
- **D3D12** (amended for the D3D12 driver) grants both features on
  resource binding tier 3, which lets tables hold unread descriptors
  uninitialized, with `heapSize` 65536 and `samplerHeapSize` 256, since
  the heaps and the frames' descriptor rings share the 2048
  shader-visible samplers a list binds; a device whose heaps would leave
  the rings fewer than 64 descriptors of a kind is refused. Every frame
  slot's shader-visible heaps hold each heap's regions at the front, at
  the heap's slot, and an entry is written into every slot's region. A
  sampled texture entry is a shader resource view and a storage texture
  an unordered access view; a storage buffer is a raw unordered access
  view when shaders may write it, else a raw shader resource view, so a
  shader reads a buffer entry through a declaration of the same
  writability, which Vulkan does not mind. The DXIL tool gives each
  heap variable a space of its own from 16, and a root signature of a
  container reading heaps adds an unbounded table per range class
  (shader resource, unordered access, sampler), pointed at the pass's
  heap after each pipeline.
- **Metal** (amended for the Metal driver) will grant heaps only on
  `MTLGPUFamilyMetal3` devices, whose descriptors (`gpuResourceID`,
  `gpuAddress`) fill a plain buffer per heap: SPIRV-Cross writes runtime
  arrays in argument buffers that way and refuses them below MSL 3.0.
  The driver grants no heap until a CI device runs that path; the
  hosted runner's device is not in the family.

## Consequences

Content picks resources by index while the graph keeps exact barriers.
Freeing an index only after earlier frames finish lets every API write
entries in place, without copies. Sealed resources give
streamed assets reads without barriers. Lavapipe runs the Vulkan path in
CI.
