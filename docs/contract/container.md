# The Maul RHI shader container

A shader container holds the SPIR-V and WGSL of a set of entry points,
made offline, beside one reflection in WebGPU's binding terms (record
mrhi-0009). `tools/mrhi_container.py` writes it, and
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
| 3 | entries | yes | 32-byte entry points, at least one |
| 4 | bindings | no | 24-byte bindings |
| 5 | vertex inputs | no | 8-byte vertex inputs |
| 6 | color outputs | no | 8-byte color outputs |
| 7 | constants | no | 16-byte specialization constants |
| 8 | SPIR-V | yes | a SPIR-V module holding every entry point |
| 9 | WGSL | yes | a WGSL module holding every entry point, UTF-8 |

An array section's size is a whole number of its records, at most
4,096 of them. Every record is checked, whether or not an entry names
it.

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

Names are unique. An entry's inputs and outputs are ranges of their
sections.

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

## Vertex inputs, color outputs and constants

| Record | Bytes | Fields |
|---|---|---|
| vertex input | 8 | location (u32), a `mrhiVertexFormat` other than none (u8), 3 zero bytes |
| color output | 8 | location (u32, below `MRHI_COLOR_TARGETS`), a `mrhiOutputKind` other than none (u8), 3 zero bytes |
| constant | 16 | id (u32, unique), a `mrhiConstantType` other than none (u8), 3 zero bytes, the default's bits (u32; 0 or 1 for a boolean), 4 zero bytes |

Locations are unique within an entry.

## Code

- **The SPIR-V section:** a whole number of 32-bit words, at least the
  five of a module's header, the first of them `0x07230203`.
- **The WGSL section:** well-formed UTF-8 without NUL.

Drivers check the code itself when they make their modules.

## The writer

`tools/mrhi_container.py SPIRV WGSL REFLECTION OUTPUT` writes a
container from the two modules and a JSON reflection; its opening
comment shows the reflection's form, whose enum names are the
contract's without their prefixes. It applies the rules above, and
refuses code that disagrees with the reflection:
- both modules hold exactly the reflection's entry points, with their
  stages, and a WGSL compute entry's literal workgroup size matches;
- every binding either module declares is in the reflection, and a
  WGSL binding's kind, texture dimension, depth, multisampling and
  sampler comparison match it.

A module may leave out a binding it does not use. The writer uses
Python's standard library only.
