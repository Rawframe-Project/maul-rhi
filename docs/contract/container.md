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
| 3 | entries | yes | 48-byte entry points, at least one |
| 4 | bindings | no | 24-byte bindings |
| 5 | vertex inputs | no | 8-byte interface records |
| 6 | color outputs | no | 8-byte interface records |
| 7 | constants | no | 16-byte specialization constants |
| 8 | SPIR-V | yes | a SPIR-V module holding every entry point |
| 9 | WGSL | yes | a WGSL module holding every entry point, UTF-8 |
| 10 | inter-stage variables | no | 8-byte interface records |

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
| 32 | u16 | its first inter-stage variable |
| 34 | u16 | its inter-stage variable count: a vertex entry's outputs or a fragment entry's inputs; 0 for compute |
| 36 | u32 | the builtins it uses, some `mrhiShaderBuiltins` bits; 0 unless it is a fragment entry |
| 40 | u32 | its workgroup storage in bytes; 0 unless it is a compute entry |
| 44 | u32 | zero |

Names are unique. An entry's inputs, outputs and variables are ranges
of their sections.

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
- 16-bit floats without `shaderF16`, or `mrhi_builtinPrimitiveIndex`.

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

Drivers check the code itself when they make their modules.

## The writer

`tools/mrhi_container.py SPIRV WGSL REFLECTION OUTPUT` writes a
container from the two modules and a JSON reflection; its opening
comment shows the reflection's form, whose enum names are the
contract's without their prefixes. It applies the rules above, and
refuses code that disagrees with the reflection:
- both modules hold exactly the reflection's entry points, with their
  stages; a WGSL compute entry's workgroup size is literal, since no
  override may change the reflection's, and matches;
- every binding either module declares is in the reflection, and a
  WGSL binding's kind, texture dimension, depth, multisampling and
  sampler comparison match it.

A module may leave out a binding it does not use. The writer uses
Python's standard library only.
