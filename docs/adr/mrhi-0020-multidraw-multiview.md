# mrhi-0020. Counted multi-draw indirect and multiview

Status: Accepted

## Context

The feature list has long named two optional capabilities with no API:
counted multi-draw indirect, which GPU-driven renderers use to draw
what a culling pass wrote, the number of draws included; and
multiview, which renders one pass into several layers for stereo and
cube maps. Vulkan has both directly. Direct3D 12 has the first through
`ExecuteIndirect` with a count buffer and the second through view
instancing, at most four views. Metal has neither directly. WebGPU has
neither, and WGSL has no view index.

## Decision

- **Counted multi-draw** (`mrhiDrawIndirectCount`,
  `mrhiDrawIndexedIndirectCount`, feature `multiDrawIndirectCount`):
  up to `maxCount` packed records of the single indirect draws'
  layouts, the number drawn read on the GPU from a 32-bit count and
  clamped to `maxCount`, at most `MRHI_INDIRECT_DRAWS` (65535), and a
  frame's `maxCount`s in all within the device limit
  `frameIndirectDraws`, which sizes the drivers' scratch. Both
  buffers are declared with the indirect access; they may be one.
  Vulkan grants it with `drawIndirectCount` and `multiDrawIndirect`.
  Direct3D 12 grants it through `ExecuteIndirect`, records expanded
  first by a compute pass of the library's for pipelines that read
  their first vertex or instance. Metal grants it emulated: a compute
  pass of the library's copies the records into scratch, every record
  past the count drawing no instances, then `maxCount` indirect draws
  run from the scratch. WebGPU never grants it.
- **Multiview** (feature `multiview`): a pass's `viewCount` renders it
  into that many consecutive layers of its targets, pipelines made for
  the same count, shader entries that read the view index marked in
  their container and without WGSL. Vulkan grants it through the
  rendering view mask; Direct3D 12 with view instancing, four views;
  Metal by vertex amplification onto layers, as many views as the
  device amplifies to, whose MSL the writer's tool makes by replacing
  SPIRV-Cross's constant view index with the amplification id (vertex)
  or the render target array index (fragment); WebGPU never. On Metal,
  draws and indirect records stay as they are; instanced views, the
  other way, would have changed every vertex entry of a multiview
  pipeline and every indirect draw.
- Each lands in slices; until a driver's slice lands, that driver does
  not grant the feature.

## Consequences

The emulations need nothing of the pipelines a program makes, and
their costs are bounded by numbers the program chooses: `maxCount`
draws encoded on Metal whatever the count, and on D3D12 a dispatch per
counted draw of a pipeline reading its first vertex or instance. The
views a device renders differ more (four on D3D12, the amplification
counts on Metal, often two), which `multiviewViews` reports. A program
reads the features and limits to know what to use, as for every
optional capability.
