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
  barriers; an entry of a resource the pass does not declare is hidden
  from it unless the resource is sealed.
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
- **Drivers** get a heap handle per pass and write entries as the core
  accepts them (`createHeap`, `destroyHeap`, `writeHeapEntry`,
  `writeHeapSampler`; SPI version 2). The test driver checks every
  handle it is given.

## Consequences

Content picks resources by index while the graph keeps exact barriers.
Freeing an index only after earlier frames finish lets every API write
entries in place, without copies. Sealed resources give
streamed assets reads without barriers. The shader reflection of heap
use and the Vulkan heap path follow in their slices.
