# The Maul RHI shader container

A shader container holds the SPIR-V, WGSL and Metal code of a set of
entry points, made offline, beside one reflection in WebGPU's binding
terms (record mrhi-0009). `tools/mrhi_container.py` writes it, and
`mrhiCreateShader` reads it, checking every byte as hostile input.

All numbers are little-endian. `mrhiCreateShader` takes the container
8-byte aligned in memory, so that its sections are too, and drivers can
hand the SPIR-V words on as they are.

## Header

The first 64 bytes:

| Offset | Type | Field |
|---|---|---|
| 0 | 4 bytes | the magic, `MRSC` |
| 4 | u32 | the container version, 1 |
| 8 | u64 | the container's size in bytes |
| 16 | 32 bytes | the SHA-256 digest of the bytes from offset 48 to the end |
| 48 | u32 | the number of sections |
| 52 | u32 | zero |
| 56 | u64 | zero |

A reader refuses:
- another magic;
- another version (`mrhi_errorVersion`);
- a size other than the bytes it was given;
- a digest that does not match;
- a zero field that is not zero.

## Sections

The section table follows the header: one 24-byte record per section.

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | the section's type |
| 4 | u32 | zero |
| 8 | u64 | its offset from the container's start, a multiple of 8 |
| 16 | u64 | its size in bytes |

Sections lie after the table, inside the container, and never overlap.
Each type appears at most once. A reader skips types it does not know,
after checking their bounds, so a later writer can add sections.

| Type | Section | Required | Contents |
|---|---|---|---|
| 1 | meta | yes | 16 bytes: the root block's size (u32, a multiple of 4, at most 256), then 12 zero bytes |
| 2 | strings | yes | UTF-8 names, referenced by offset and length |
| 3 | entries | yes | 48-byte entry points, at least one |
| 4 | bindings | no | 24-byte bindings |
| 5 | vertex inputs | no | 8-byte interface records |
| 6 | color outputs | no | 8-byte interface records |
| 7 | constants | no | 16-byte specialization constants |
| 8 | SPIR-V | yes | a SPIR-V module holding every entry point |
| 9 | WGSL | unless an entry uses a heap | a WGSL module holding every entry point, UTF-8 |
| 10 | inter-stage variables | no | 8-byte interface records |
| 11 | Metal map | with Metal code | where each binding lies in Metal, and each entry's MSL |
| 12 | MSL | no | Metal Shading Language sources, UTF-8 |
| 13 | metallib | no | a Metal library holding every entry point |
| 14 | D3D12 map | with DXIL | where each binding lies in D3D12, and each entry's DXIL |
| 15 | DXIL | no | a DXIL container per entry point |

An array section's size is a whole number of its records, at most
4,096 of them. Every record is checked, whether or not an entry names
it.

Metal code is optional: a container has the Metal map exactly when it
has MSL, a metallib, or both, and none of its entries uses a heap.

## Entries

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | its stage: one `mrhiShaderStages` bit |
| 4 | u32 | its name's offset in the strings |
| 8 | u32 | its name's length, 1 to 256 bytes, UTF-8 without NUL |
| 12 | 3 × u32 | its workgroup size: each at least 1 for compute, 0 otherwise |
| 24 | u16 | its first vertex input |
| 26 | u16 | its vertex input count, 0 unless it is a vertex entry |
| 28 | u16 | its first color output |
| 30 | u16 | its color output count, 0 unless it is a fragment entry |
| 32 | u16 | its first inter-stage variable |
| 34 | u16 | its inter-stage variable count: a vertex entry's outputs or a fragment entry's inputs; 0 for compute |
| 36 | u32 | the builtins it uses, some `mrhiShaderBuiltins` bits: a fragment entry's any, a vertex entry's only the view index, a compute entry's none |
| 40 | u32 | its workgroup storage in bytes; 0 unless it is a compute entry |
| 44 | u32 | the heaps it reads, some `mrhiShaderHeapUses` bits |

Names are unique. An entry's inputs, outputs and variables are ranges
of their sections.

An entry's heap uses name what it reads through the pass's heap (record
mrhi-0015): sampled textures, storage textures and storage buffers from
the resource heap, and samplers from the sampler heap. `writes` goes
with a storage kind, and never in a vertex entry, as WebGPU requires of
bound storage. SPIR-V reads the resource heap at set 4 binding 0, with
the types an entry reads aliased there, and the sampler heap at set 4
binding 1; DXC places SM 6.6 heaps there with
`-fvk-bind-resource-heap 0 4 -fvk-bind-sampler-heap 1 4`.

An entry that reads the view index of a multiview pass (record
mrhi-0020) has the `mrhi_builtinViewIndex` bit, in a vertex or fragment
entry; SPIR-V reads it as the `ViewIndex` builtin with the `MultiView`
capability, and a device loads the container only with the multiview
feature. WGSL reads no heaps yet and has no view index, so a container
has a WGSL section exactly when none of its entries uses a heap or the
view index.

## Bindings

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | its table, 0 to 3 |
| 1 | u8 | its kind, a `mrhiBindingKind` other than none |
| 2 | u16 | its slot, unique in its table |
| 4 | u32 | the stages that use it, some `mrhiShaderStages` bits |
| 8 | u8 | a sampler's `mrhiSamplerBinding`; else 0 |
| 9 | u8 | a sampled texture's `mrhiSampleType`; else 0 |
| 10 | u8 | a texture's view dimension, a `mrhiTextureKind`; else 0 |
| 11 | u8 | a storage texture's `mrhiStorageAccess`; else 0 |
| 12 | u16 | a storage texture's `mrhiFormat`; else 0 |
| 14 | u8 | 1 for a multisampled texture, else 0 |
| 15 | u8 | zero |
| 16 | u64 | a buffer's minimum size in bytes; else 0 |

As WebGPU requires:
- a storage texture is 2D, a 2D array or 3D;
- a multisampled texture is 2D and not filterable;
- neither a writable storage buffer nor a written storage texture is
  used by the vertex stage.

## Interface records

Vertex inputs, color outputs and inter-stage variables share one
record:

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | its location |
| 4 | u8 | its `mrhiScalarType`, other than none |
| 5 | u8 | its component count, 1 to 4 |
| 6 | u8 | an inter-stage variable's `mrhiInterpolation`; else 0 |
| 7 | u8 | an inter-stage variable's `mrhiSampling`; else 0 |

- A vertex input is a 32-bit float, signed or unsigned integer.
- A color output's location is below `MRHI_COLOR_TARGETS`.
- An inter-stage variable is interpolated as WGSL allows: integers
  flat; perspective or linear sampled at the center, the centroid or
  per sample; flat taken from the first or either vertex.
- Locations are unique within an entry's inputs, its outputs and its
  variables.

## The Metal map

Metal binds by index, per class of argument: buffers (0 to 30),
textures (0 to 127) and samplers (0 to 15). The map says where the
root block and each binding lie, and where each entry's MSL is:

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | the root block's buffer index; 255 when the root block is empty |
| 1 | 7 bytes | zero |
| 8 | 16 bytes per entry | the entries, in the entries section's order |
| after them | u8 per binding | each binding's index in its class, in the bindings section's order |

Its size is exactly that. An entry's record:

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | its MSL's offset in the MSL section |
| 4 | u32 | its MSL's length in bytes; both 0 when there is no MSL section |
| 8 | u8 | the buffer index of its buffer sizes (SPIRV-Cross's `spvBufferSizeConstants`); 255 when it has none |
| 9 | 7 bytes | zero |

- Buffers (uniform, storage and read-only storage) take buffer indices,
  sampled and storage textures texture indices, samplers sampler
  indices, each within its class's range.
- No two bindings share an index in their class, and no binding buffer
  is the root block's.
- An entry's buffer sizes index is a buffer index no binding and not
  the root block uses.
- With an MSL section, every entry's MSL lies inside it and is
  well-formed UTF-8 without NUL, holding that entry as a function of
  the same name; SPIRV-Cross writes one entry per source, so each has
  its own. A metallib holds every entry as a function of its name, and
  begins with `MTLB`.

Vertex buffers take buffer indices from 30 downwards, vertex buffer 0
at 30; the Metal driver refuses a pipeline whose vertex buffers reach
an index its container uses.

## The D3D12 map

D3D12 binds by register and space, per class: constant buffers (`b`),
shader resource views (`t`), unordered access views (`u`) and samplers
(`s`). Uniform buffers are constant buffers; sampled textures and
read-only storage buffers shader resource views; storage buffers and
storage textures unordered access views. The map says where the root
block, the specialization constants, the vertex information, each
binding and the heaps' ranges lie, and where each entry's DXIL is:

| Offset | Type | Field |
|---|---|---|
| 0 | 2 × u32 | the root block's constant buffer register and space; both 0 when the root block is empty |
| 8 | 2 × u32 | the constants' constant buffer register and space; both 0 without constants |
| 16 | 2 × u32 | the vertex information's constant buffer register and space; both 0 when no entry reads it |
| 24 | u32 | the heaps' range count |
| 28 | 4 bytes | zero |
| 32 | 16 bytes per entry | the entries, in the entries section's order |
| after them | 2 × u32 per binding | each binding's register and space, in the bindings section's order |
| after them | 16 bytes per range | the heaps' ranges |
| after them | u8 per constant | 1 when the constant is fixed, else 0, in the constants section's order |

Its size is exactly that. An entry's record:

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | its DXIL's offset in the DXIL section, a multiple of 4 |
| 4 | u32 | its DXIL's length in bytes |
| 8 | u32 | 1 when it reads the vertex information, else 0; 0 unless it is a vertex entry |
| 12 | 4 bytes | zero |

A range of the heaps, an unbounded array of descriptors the code reads
from its register on:

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | its class: 0 shader resource views and 1 unordered access views, both from the resource heap, or 2 samplers from the sampler heap |
| 4 | u32 | its register |
| 8 | u32 | its space |
| 12 | 4 bytes | zero |

- The constants' constant buffer holds each constant's 32 bits in the
  constants section's order, constant i at byte 4i. A fixed constant
  was compiled with its default, which it must have: the code does not
  read it, and a pipeline may not give it another value.
- The vertex information's constant buffer holds the draw's base
  vertex, then its first instance, as 32-bit integers, which D3D12's
  vertex and instance ids leave out.
- The root block, the constants, the vertex information and the
  bindings lie in spaces below 16, and no two of them share a register
  and space in their class. The heaps' ranges lie in spaces from 16 and
  below `0xFFFFFFF0`, and no two of one class share a register and
  space.
- There is a sampler range exactly when an entry reads samplers from
  the heap, and a range of views exactly when one reads resources from
  it.
- Every entry's DXIL lies inside the DXIL section and is a DXIL
  container: it begins with `DXBC` and its size field (the u32 at byte
  24) is its length.

## On a device

`mrhiCreateShader` refuses as unsupported a container that exceeds the
device:
- its root block, a binding's slot or minimum size, or a stage's count
  of any binding kind (read-only storage buffers count as storage
  buffers) past the limits;
- a compute entry's workgroup size in any dimension, invocations or
  workgroup storage past the limits;
- a vertex input location, or an inter-stage variable location, past
  the limits, or a fragment entry reading more inter-stage variables,
  with the builtins `front_facing`, `sample_index`, `sample_mask` and
  `primitive_index` counted, than the limit;
- 16-bit floats without `shaderF16`, or `mrhi_builtinPrimitiveIndex`;
- heap uses without `bindless_sampling`, or storage textures or buffers
  from the heap without `bindless_heterogeneous`.

A pipeline's binding layout is the whole container's, so these are the
same for every pipeline made from it.

## Constants

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | its id, unique |
| 4 | u8 | its `mrhiConstantType`, other than none |
| 5 | 3 bytes | zero |
| 8 | u32 | its default's bits: 0 or 1 for a boolean, 0 when it has none |
| 12 | u8 | 1 when it has no default, so every pipeline sets it; else 0 |
| 13 | 3 bytes | zero |

## Code

- **The SPIR-V section:** a whole number of 32-bit words, at least the
  five of a module's header, the first of them `0x07230203`.
- **The WGSL section:** well-formed UTF-8 without NUL.
- **The MSL section:** its entries' ranges, as the Metal map says.
- **The metallib section:** at least its four-byte magic.
- **The DXIL section:** its entries' containers, as the D3D12 map says.

Drivers check the code itself when they make their modules.

## The writer

`tools/mrhi_container.py [--msl DIR] [--metallib FILE] [--dxil DIR]
SPIRV WGSL REFLECTION OUTPUT` writes a container from the two modules
and a JSON reflection (`-` for the WGSL of a container whose entries
use heaps); its opening comment shows the reflection's form, whose enum
names are the contract's without their prefixes. With `--msl`, it reads
each entry's MSL from `DIR/ENTRY.metal`; with `--metallib`, the
library; with either, it writes the Metal map by the rule below. With
`--dxil`, it reads each entry's DXIL from `DIR/ENTRY.dxil` and writes
the D3D12 map by the rule below. It applies the rules above, and
refuses code that disagrees with the reflection:
- both modules hold exactly the reflection's entry points, with their
  stages; a WGSL compute entry's workgroup size is literal, since no
  override may change the reflection's, and matches;
- every binding either module declares is in the reflection, and a
  WGSL binding's kind, texture dimension, depth, multisampling and
  sampler comparison match it; SPIR-V binds set 4 binding 0 only when
  an entry reads resources from the heap, and binding 1 only when one
  reads samplers.

A module may leave out a binding it does not use. The writer uses
Python's standard library only.

The writer's Metal rule: the root block at buffer 0; bindings in
(table, slot) order, buffers from index 1, textures and samplers from
0. An entry's MSL must declare its function with its stage's keyword
(`vertex`, `fragment` or `kernel`) and use no buffer, texture or sampler
index outside the map, except its buffer sizes, which the writer reads
from the MSL. `tools/mrhi_msl.py SPIRV REFLECTION DIR` makes that MSL
with SPIRV-Cross: it rewrites a copy of the SPIR-V's descriptor
decorations to the rule's indices and crosses each entry to
`DIR/ENTRY.metal`.

The writer's D3D12 rule: a binding's register is its slot and its
space its table; the root block, the constants and the vertex
information are constant buffers 0, 1 and 2 of space 5. A constant is
fixed when the SPIR-V sizes something with it (a workgroup size or an
array's length, directly or through other constants), since HLSL needs
a literal there. The writer reads each entry's resources from its DXIL
(the `PSV0` part) and refuses DXIL of another stage or reading a
resource outside the map, including the vertex information outside a
vertex entry; an entry reads the vertex information when its DXIL
declares that constant buffer. A resource in a space from 16 is a
range of the heaps, which only an entry reading that heap may declare;
the writer lists the ranges every entry declares, by class, space and
register. `tools/mrhi_dxil.py SPIRV REFLECTION DIR` makes that DXIL for
shader model 6.0 with SPIRV-Cross and DXC: it gives a copy of the
SPIR-V's root block the rule's register, moves each heap variable to
register 0 of a space of its own from 16, in the order the module
declares them, so that arrays of one class never collide, crosses each
entry to HLSL that reads the vertex information, defines each unfixed
constant as a read of the constants' buffer, and compiles
`DIR/ENTRY.dxil` without reflection or debug data.
