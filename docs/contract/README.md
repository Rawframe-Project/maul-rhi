# The Maul RHI contract

`mrhi.json` is the source of truth for the public headers and the
thread safety table (record mrhi-0002). Edit it, then run
`tools/gen_contract.py`; the gate and CI run it with `--check`.

## The file

| Key | Contents |
|---|---|
| `library`, `title` | The repository name and the display name. |
| `prefix`, `macro`, `guard` | `mrhi`, `MRHI` and `MAUL_RHI`: the C name prefixes. |
| `version` | `major`, `minor`, `patch`: the library version. |
| `contract_version` | The contract version, `MRHI_CONTRACT_VERSION`. |
| `headers` | The public headers, in order, each with a `name`, a `doc`, an optional `preamble` and its `items`. |

A header's `preamble` lists the fixed blocks it opens with: `version`
(the library and contract version macros), `api` (the export macro)
and `nodiscard`. `includes` lists the other headers it needs.

## Items

Every item has a `kind`, a snake_case `name` and a `doc`.

| Kind | Fields | C form |
|---|---|---|
| `result` | `values`: `name`, `value`, `doc` | a fixed-width `int32_t` type and its values; `success` is zero |
| `enum` | `width` (`uint8`, `uint16`, `uint32`, `int32`), `values` | a fixed-width type and its values |
| `bitflags` | `width`, `values`, each a single bit | a fixed-width type and its bits; the core refuses bits the contract does not list |
| `constant` | `value`, a non-negative integer | a macro, `MRHI_` and the name in capitals |
| `opaque` | none | a typed opaque pointer's struct, for a root object |
| `id` | none | a generation-checked id, `{ index1, generation }` (family record 0016) |
| `struct` | `members`: `name`, `type`, optional `doc`, `pointer`; optional `def` or `chained` | a typedef struct; a def opens with `cookie` and `next` (mrhi-0005), a chained struct with `mrhiChain chain` |
| `function` | `args` (`name`, `type`, `doc`), optional `returns` (`type`, `doc`), `thread_safety` | a documented declaration; one returning `result` is nodiscard |

C names follow `docs/conventions.md` section 4: `get_version` becomes
`mrhiGetVersion`, the struct `version` becomes `mrhiVersion`, the value
`error_invalid` becomes `mrhi_errorInvalid`, and a member or argument
`view_count` becomes `viewCount`.

## Types

A type is a primitive (`bool`, `char`, `int8` to `int64`, `uint8` to
`uint64`, `size`, `float32`, `float64`, `static_cstring`), `result`, `void`, or a
reference to an item as `kind.name`, such as `struct.version`. A member
or argument may add `pointer`: `const` (`const T*`), `mutable` (`T*`)
or `out` (`T**`, where a function hands back a new root). `void` and
opaque types are only used through a pointer.

A member of type `function` is a function pointer, with `returns` and
`args` as a function has, without docs. A member with `array` naming a
constant is a fixed array of that length, such as a name's bytes.

Headers go through the pinned clang-format (22.1.5, as CI installs it),
which the generator needs on the path.

## Mappings

Every `function`, `struct`, `enum` and `bitflags` is classed one way:

- `"mapped": true`, with a `mapping` on each value or member (a value
  or member with `"unmapped": true` is skipped);
- `mapping` on the item itself, taking it whole: one row per API;
- `"library"`, the reason no API maps it (the library's bookkeeping).

A `mapping` has a row for each of `vulkan`, `d3d12`, `metal` and
`web_gpu`: a `class` (`direct`, `emulated`, `restricted` or
`absent_rejected`), a `note` on how, and for an emulated row its
`cost`. The generator refuses an item classed no way or more than one.
`docs/contract/mappings.md` shows
the rows, and each header's whole concepts and the library's own.

## Thread safety

`thread_safety` has a `class` and, where it applies, an `object`
(with `plural` when it names several) and a `note`:

| Class | Opening |
|---|---|
| `any` | Safe from any thread. |
| `any_exclusive` | Safe from any thread; `object` is used by one thread at a time. |
| `main` | Main thread only. |
| `realtime` | Real-time safe: no allocation, lock or wait. |

## Mappings, defaults and limits

A struct with `mapped` gives every member a `mapping`: a row for each
of `vulkan`, `d3d12`, `metal` and `web_gpu`, with a `class` (`direct`,
`emulated`, `restricted`, `absent_rejected`) and a `note` on how. They
make `docs/contract/mappings.md`, and the core masks off the features a
driver's API has classed absent-rejected.

A struct with `defaults` gives every member a `default`, which makes its
Default function, generated in `src/generated/defaults.c`. A member may
add `better: lower` (the alignments); the others are better higher.

## Formats

A value of the `format` enum listed for textures carries, besides its
mapping:

| Field | Meaning |
|---|---|
| `floor` | the capabilities every listed adapter has (WebGPU's guarantees) |
| `block` | the width and height of a block in texels, `[1, 1]` if uncompressed |
| `family` | the feature a compressed format needs |
| `srgb_pair` | its sRGB or linear twin, the one format its views may change to |
| `aspects` | `depth` and `stencil` for depth formats; a color format has none |

They make `src/generated/capabilities.c`: the known formats, the floor,
the family and block, the twin, and the aspect checks.
