# Shader container format

The byte layout of a shader container, version 2, for anyone writing a cook of their own instead of `tools/mrhi_container.py`. [Shaders and pipelines](shaders.md) explains what a container is for and how to make one with the library's tools.

## Contents

- [Layout](#layout)
- [Header](#header)
- [Section table](#section-table)
- [Entries](#entries)
- [Bindings](#bindings)
- [Interface records](#interface-records)
- [Constants](#constants)
- [The Metal map](#the-metal-map)
- [The D3D12 map](#the-d3d12-map)
- [Code sections](#code-sections)
- [Limits checked on a device](#limits-checked-on-a-device)
- [The writer's placement rules](#the-writers-placement-rules)

## Layout

Every number is little-endian, and `mrhiCreateShader` reads byte by byte, so a container means the same on every host. The program hands the container over 8-byte aligned, so that its sections are aligned too and drivers can pass SPIR-V words on as they are. Every byte is checked as hostile input before anything is kept: a container that breaks any rule below is `mrhi_errorFormat`, counting no misuse, one of another version or holding a section type the library does not know is `mrhi_errorVersion`, and one that needs what the device lacks is `mrhi_errorUnsupported`.

A container is a header, a table of sections, and the sections. The sections are its fields: each is named by its type, a 32-bit tag, and the format grows by new types, never by longer records, so a record whose meaning changes gets a section of a new type. Array sections are made of fixed-size records, at most 4096 of each, and every record is checked whether or not an entry refers to it. Names live in one strings section and are referred to by offset and length.

## Header

The first 64 bytes:

| Offset | Type | Field |
|---|---|---|
| 0 | 4 bytes | `MRSC` |
| 4 | u32 | the format version, 2 |
| 8 | u64 | the container's size in bytes, which must be the size handed over |
| 16 | 32 bytes | the SHA-256 digest of every byte from offset 48 to the end |
| 48 | u32 | the number of sections, at most 64 |
| 52 | 12 bytes | zero |

The digest identifies the container (pipeline caches and a cook's outputs are keyed by it) and catches damage in transit.

## Section table

One 24-byte record per section follows the header:

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | the section type |
| 4 | u32 | zero |
| 8 | u64 | the section's offset from the container's start, a multiple of 8 |
| 16 | u64 | its size in bytes |

The sections fill the container after the table: the table may list them in any order, but laid out by offset, the first starts right after the table and each one starts at the first multiple of 8 after the one before ends, with zero bytes in between; the last one's padding ends the container. A type appears at most once, and no section is empty.

A section holding its default is left out, and a reader gives an absent section its default: the meta's is a root block of 0 bytes, and an array or code section's is empty. A writer therefore never writes an empty section or a meta of 0 bytes, and a reader refuses either, so a container has one encoding and one digest. A type the reader does not know is `mrhi_errorVersion`, since only a later version writes one.

| Type | Section | Present | Contents |
|---|---|---|---|
| 1 | meta | with a root block | 16 bytes: the root block's size (u32, a multiple of 4, 4 to 256), then 12 zero bytes |
| 2 | strings | always | UTF-8 names |
| 3 | entries | always | 48-byte entry points, at least one |
| 4 | bindings | when there are any | 24-byte bindings |
| 5 | vertex inputs | when there are any | 8-byte interface records |
| 6 | color outputs | when there are any | 8-byte interface records |
| 7 | constants | when there are any | 16-byte specialization constants |
| 8 | SPIR-V | always | one module holding every entry point |
| 9 | WGSL | see below | one module holding every entry point |
| 10 | inter-stage variables | when there are any | 8-byte interface records |
| 11 | Metal map | with Metal code | where each binding lies in Metal, and each entry's MSL |
| 12 | MSL | optional | Metal Shading Language sources |
| 13 | metallib | optional | a Metal library of every entry point |
| 14 | D3D12 map | with DXIL | where each binding lies in D3D12, and each entry's DXIL |
| 15 | DXIL | optional | a DXIL container per entry point |

The WGSL section is present exactly when no entry reads a heap or the view index and the SPIR-V does not declare `Int64`, since WGSL has none of them. The Metal map is present exactly when there is MSL, a metallib or both, and no entry reads a heap. A container without the code its driver needs is refused by that driver as unsupported.

## Entries

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | the stage, one `mrhiShaderStages` bit |
| 4 | u32 | the name's offset in the strings |
| 8 | u32 | the name's length, 1 to 256 bytes of UTF-8 without NUL; names are unique |
| 12 | 3 × u32 | the workgroup size: each at least 1 for compute, 0 otherwise |
| 24 | u16, u16 | the first vertex input and the count, 0 unless a vertex entry |
| 28 | u16, u16 | the first color output and the count, 0 unless a fragment entry |
| 32 | u16, u16 | the first inter-stage variable and the count: a vertex entry's outputs or a fragment entry's inputs, 0 for compute |
| 36 | u32 | the builtins used, `mrhiShaderBuiltins` bits: any for a fragment entry, only the view index for a vertex entry, none for compute |
| 40 | u32 | the workgroup storage in bytes, 0 unless a compute entry |
| 44 | u32 | what the entry reads through the pass's heap, `mrhiShaderHeapUses` bits |

Heap uses name sampled textures, storage textures and storage buffers read from the resource heap, and samplers read from the sampler heap. The `writes` bit goes with a storage kind and never in a vertex entry. SPIR-V reads the resource heap at set 4 binding 0, with every type an entry reads aliased there, and the sampler heap at set 4 binding 1, where DXC places Shader Model 6.6 heaps with `-fvk-bind-resource-heap 0 4 -fvk-bind-sampler-heap 1 4`.

An entry that reads a multiview pass's view index has the `mrhi_builtinViewIndex` bit; its SPIR-V reads the `ViewIndex` builtin with the `MultiView` capability.

## Bindings

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | the table, 0 to 3 |
| 1 | u8 | the kind, an `mrhiBindingKind` other than none |
| 2 | u16 | the slot, unique within the table |
| 4 | u32 | the stages that use it, `mrhiShaderStages` bits |
| 8 | u8 | a sampler's `mrhiSamplerBinding`, else 0 |
| 9 | u8 | a sampled texture's `mrhiSampleType`, else 0 |
| 10 | u8 | a texture's view dimension, an `mrhiTextureKind`, else 0 |
| 11 | u8 | a storage texture's `mrhiStorageAccess`, else 0 |
| 12 | u16 | a storage texture's `mrhiFormat`, else 0 |
| 14 | u8 | 1 for a multisampled texture, else 0 |
| 15 | u8 | zero |
| 16 | u64 | a buffer's minimum size in bytes, else 0 |

WebGPU's rules apply: a storage texture is 2D, a 2D array or 3D; a multisampled texture is 2D and not filterable; the vertex stage uses no writable storage buffer and no written storage texture.

## Interface records

Vertex inputs, color outputs and inter-stage variables share one 8-byte record:

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | the location |
| 4 | u8 | the `mrhiScalarType`, other than none |
| 5 | u8 | the component count, 1 to 4 |
| 6 | u8 | an inter-stage variable's `mrhiInterpolation`; a color output's blend source, 0 or 1; else 0 |
| 7 | u8 | an inter-stage variable's `mrhiSampling`, else 0 |

- A vertex input is a 32-bit float or a signed or unsigned 32-bit integer.
- A color output's location is below `MRHI_MAX_COLOR_TARGETS`.
- Inter-stage variables are interpolated as WGSL allows: integers flat; perspective or linear at the center, the centroid or per sample; flat from the first vertex or either vertex.
- Locations are unique within an entry's inputs, outputs and variables, except for dual-source blending: a color output of blend source 1 sits at location 0 beside one of source 0 with the same type and component count. A pipeline whose blending reads the second source needs `dualSourceBlending`, exactly one color target, and such a fragment entry.

## Constants

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | the id, unique |
| 4 | u8 | the `mrhiConstantType`, other than none |
| 5 | 3 bytes | zero |
| 8 | u32 | the default's bits: 0 or 1 for a boolean, 0 when there is no default |
| 12 | u8 | 1 when there is no default, so every pipeline must set it, else 0 |
| 13 | 3 bytes | zero |

## The Metal map

Metal binds by index within three classes: buffers 0 to 30, textures 0 to 127 and samplers 0 to 15. The map is exactly:

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | the root block's buffer index, 255 when the root block is empty |
| 1 | 7 bytes | zero |
| 8 | 16 bytes per entry | in the entries section's order, as below |
| after them | u8 per binding | each binding's index in its class, in the bindings section's order |

An entry's record:

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | its MSL's offset in the MSL section |
| 4 | u32 | its MSL's length; both 0 when there is no MSL section |
| 8 | u8 | the buffer index of its buffer sizes (SPIRV-Cross's `spvBufferSizeConstants`), 255 for none |
| 9 | 7 bytes | zero |

- Uniform, storage and read-only storage buffers take buffer indices; sampled and storage textures texture indices; samplers sampler indices.
- No two bindings share an index in their class, no binding uses the root block's buffer, and an entry's buffer sizes index is one nothing else uses.
- Each entry's MSL is well-formed UTF-8 without NUL inside the MSL section, holding that entry as a function of its name; SPIRV-Cross writes one entry per source, so each entry has its own. A metallib holds every entry under its name and begins with `MTLB`.

Vertex buffers take buffer indices from 30 downwards, vertex buffer 0 at 30. The Metal driver refuses a pipeline whose vertex buffers reach an index its container uses.

## The D3D12 map

D3D12 binds by register and space within four classes: constant buffers (`b`), shader resource views (`t`), unordered access views (`u`) and samplers (`s`). Uniform buffers are constant buffers; sampled textures and read-only storage buffers shader resource views; storage buffers and storage textures unordered access views. The map is exactly:

| Offset | Type | Field |
|---|---|---|
| 0 | 2 × u32 | the root block's constant buffer register and space, both 0 when the root block is empty |
| 8 | 2 × u32 | the constants' constant buffer register and space, both 0 without constants |
| 16 | 2 × u32 | the vertex information's constant buffer register and space, both 0 when no entry reads it |
| 24 | u32 | the number of heap ranges |
| 28 | 4 bytes | zero |
| 32 | 16 bytes per entry | in the entries section's order, as below |
| after them | 2 × u32 per binding | each binding's register and space, in the bindings section's order |
| after them | 16 bytes per range | the heap ranges, as below |
| after them | u8 per constant | 1 when the constant is fixed, else 0, in the constants section's order |

An entry's record: its DXIL's offset in the DXIL section (u32, a multiple of 4), its length (u32), 1 when it reads the vertex information (u32, only a vertex entry may), and 4 zero bytes.

A heap range, an unbounded array of descriptors the code reads from its register on: its class (u32: 0 shader resource views and 1 unordered access views from the resource heap, 2 samplers from the sampler heap), its register (u32), its space (u32) and 4 zero bytes.

- The constants' buffer holds constant i's 32 bits at byte 4i. A fixed constant was compiled with its default, which it must have; a pipeline may not give it another value.
- The vertex information's buffer holds the draw's base vertex, then its first instance, as 32-bit integers, which D3D12's vertex and instance ids leave out.
- The root block, the constants, the vertex information and the bindings lie in spaces below 16, and no two share a register and space in their class. Heap ranges lie in spaces from 16 to below `0xFFFFFFF0`, and no two of one class share a register and space.
- There is a sampler range exactly when an entry reads samplers from the heap, and a view range exactly when one reads resources from it.
- Each entry's DXIL lies inside the DXIL section and is a DXIL container: it begins with `DXBC`, and its size field (the u32 at byte 24) is its length.

## Code sections

- **SPIR-V:** a whole number of 32-bit words, at least the five of a module's header, the first `0x07230203`.
- **WGSL:** well-formed UTF-8 without NUL.
- **MSL, metallib, DXIL:** as their maps say.

The library checks the structure; each driver's API checks the code itself when the driver makes its modules.

## Limits checked on a device

`mrhiCreateShader` refuses as unsupported a container the device cannot run:

- the root block, a binding's slot or minimum size, or a stage's count of any binding kind past the device's limits (read-only storage buffers count as storage buffers);
- a compute entry's workgroup size, invocations or workgroup storage past the limits;
- a vertex input or inter-stage variable location past the limits, or a fragment entry reading more inter-stage variables than the limit, counting the builtins `front_facing`, `sample_index`, `sample_mask` and `primitive_index`;
- 16-bit floats without `shaderF16`, or `mrhi_builtinPrimitiveIndex`;
- code needing a feature the device lacks, read from the capabilities the SPIR-V declares before its other instructions: `Float16` and the 16-bit storage capabilities (4433 to 4436) need `shaderF16`, `Int64` needs `shaderInt64`, and `GroupNonUniform` to `GroupNonUniformQuad` (61 to 68) need `subgroups`;
- the view index without `multiview`; heap uses without `bindlessSampling`, or storage textures or buffers from the heap without `bindlessHeterogeneous`.

A pipeline's binding layout is the whole container's, so these hold for every pipeline made from it.

## The writer's placement rules

`tools/mrhi_container.py` places bindings by fixed rules, which `tools/mrhi_msl.py` and `tools/mrhi_dxil.py` follow when they make the native code. A cook of its own may place them otherwise, as long as its maps say where.

- **Metal:** the root block at buffer 0; bindings in (table, slot) order, buffers from index 1, textures and samplers from 0. Each entry's MSL declares its function with its stage's keyword (`vertex`, `fragment` or `kernel`) and uses no index outside the map except its buffer sizes, which the writer reads from the MSL.
- **D3D12:** a binding's register is its slot and its space its table; the root block, the constants and the vertex information are constant buffers 0, 1 and 2 of space 5; each heap variable gets register 0 of a space of its own from 16. A constant is fixed when the SPIR-V sizes something with it (a workgroup size or an array length, directly or through other constants), since HLSL needs a literal there. The writer reads each entry's resources from its DXIL's `PSV0` part and refuses DXIL of another stage or reading anything outside the map.
