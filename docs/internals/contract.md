# The contract

`contract/mrhi.json` is the public API as data. `tools/gen_contract.py` writes the public headers in `include/maul-rhi/` and four sources in `src/generated/` from it, and CI fails when any of them differs from what the contract makes. A change to the API is an edit to the contract and a regeneration.

## Contents

- [Why a contract](#why-a-contract)
- [The file](#the-file)
- [Items](#items)
- [Types](#types)
- [Mappings](#mappings)
- [Defaults, limits and formats](#defaults-limits-and-formats)
- [Thread safety](#thread-safety)
- [Versions](#versions)

## Why a contract

The library promises that every concept it exposes exists on Vulkan, Direct3D 12, Metal and WebGPU, or is refused where it does not. That promise is only checkable if each concept carries its mapping beside its declaration. The contract holds both, and the generator refuses a concept that has neither a mapping onto all four APIs nor a reason why it is the library's own. Generating the headers from it also keeps the documentation blocks, the defaults and the capability checks in step with the declarations, which hand-written headers drift from. JSON keeps the generator within Python's standard library.

The generated headers are the API programs read, so no rule is waived for them: they go through the pinned clang-format and pass the same documentation and source checks as hand-written code.

## The file

| Key | Contents |
|---|---|
| `library`, `title` | The repository name and the display name. |
| `prefix`, `macro`, `guard` | `mrhi`, `MRHI` and `MAUL_RHI`: the C name prefixes. |
| `contract_version` | `MRHI_CONTRACT_VERSION`. |
| `headers` | The public headers in order, each with a `name`, a `doc`, its `items`, an optional `preamble` and the other headers it `includes`. |

A header's `preamble` names fixed blocks it opens with: `version` (the version macros), `api` (the export macro) and `nodiscard`. The library version is not in the contract: it lives in `base.h` between release-please's markers, and the generator keeps whatever it finds there.

## Items

Every item has a `kind`, a snake_case `name` and a `doc`. The generator turns names into the family's C names: `get_version` becomes `mrhiGetVersion`, the struct `version` becomes `mrhiVersion`, the value `error_invalid` becomes `mrhi_errorInvalid`, and a member `view_count` becomes `viewCount`.

| Kind | Fields | C form |
|---|---|---|
| `result` | `values`: `name`, `value`, `doc` | an `int32_t` type and its values; `success` is zero |
| `enum` | `width` (`uint8`, `uint16`, `uint32`, `int32`), `values` | a fixed-width type and named constants |
| `bitflags` | `width`, `values`, each a single bit | a fixed-width type and its bits; the core refuses bits the contract does not list |
| `constant` | `value`, a non-negative integer | a macro, `MRHI_` and the name in capitals |
| `opaque` | none | a typed opaque pointer, for a root object |
| `id` | none | a generation-checked id, `{ index1, generation }` |
| `struct` | `members` (`name`, `type`, optional `doc` and `pointer`); optional `def` or `chained` | a typedef struct; a def opens with `cookie` and `next`, a chained struct with `mrhiChain chain` |
| `function` | `args`, optional `returns`, `thread_safety`, optional `inline_body` | a documented declaration; one returning `result` is nodiscard; one with an `inline_body` is a `static inline` function built in the program |

An `inline_body` exists for defaults that must be built in the program rather than the library, such as `mrhiDefaultInstanceDef`, which stamps the headers the program was built with.

The generator refuses a contract with structural errors before writing anything: unknown kinds or types, repeated names or values, a result without success at zero, a missing doc, an unknown thread safety class.

## Types

A type is a primitive (`bool`, `char`, `int8` to `int64`, `uint8` to `uint64`, `size`, `float32`, `float64`, `static_cstring`), `result`, `void`, or a reference to an item as `kind.name`, such as `struct.version`. A member or argument may add `pointer`: `const` (`const T*`), `mutable` (`T*`) or `out` (`T**`, for a function that hands back a new root). `void` and opaque types are used only through a pointer. A member of type `function` is a function pointer, with `returns` and `args`. A member with `array` naming a constant is a fixed array of that length.

## Mappings

Every `function`, `struct`, `enum` and `bitflags` is classed exactly one way:

- `"mapped": true`, with a `mapping` on each value or member; a value or member marked `"unmapped": true` is skipped;
- a `mapping` on the item itself, which takes it whole, one row per API;
- `"library"`, the reason no API maps it: the library's own bookkeeping.

A mapping has a row for each of `vulkan`, `d3d12`, `metal` and `web_gpu`: a `class` and a `note` on how. The classes:

- **direct:** the API has it as such.
- **emulated:** the driver builds it from other parts; the row states the `cost`.
- **restricted:** the API has it on some devices or under conditions, which the note names.
- **absent-rejected:** the API never has it. The core masks such a feature off whatever an adapter reports, and refuses what needs it.

`tools/gen_contract.py --mappings OUT.md` writes every row as a table, and `tools/site_pages.py` puts it on the documentation site. A concept enters the contract only with its four rows worked out against each API's specification.

## Defaults, limits and formats

- A struct with `defaults` gives each member a `default`, from which its `Default` function in `src/generated/defaults.c` is made. A limit member may add `better: lower`, as alignments do; every other limit is better higher, which is how the core compares a request against an adapter.
- The `format` enum's texture formats carry, beside their mapping, the capabilities every adapter has (`floor`), their block size, the feature a compressed format needs (`family`), their sRGB or linear twin (`srgb_pair`) and their depth and stencil `aspects`. They make `src/generated/capabilities.c`: the known formats, the floor, the twins and the aspect checks.
- Diagnostic codes are an enum like any other; their docs become the texts `mrhiDiagnosticText` returns, in `src/generated/diagnostic_texts.c`.

## Thread safety

Every function's `thread_safety` has a `class` and, where it applies, an `object` (with `plural` when it names several) and a `note`. The class becomes the opening of the function's thread safety paragraph, one of the statements `tools/check_docs.py` accepts:

| Class | Opening |
|---|---|
| `any` | Safe from any thread. |
| `any_exclusive` | Safe from any thread; the object is used by one thread at a time. |
| `main` | Main thread only. |
| `realtime` | Real-time safe: no allocation, lock or wait. |

## Versions

Several numbers version different things, and each changes for its own reason:

- **The library version** (`MRHI_VERSION_*` in `base.h`): Semantic Versioning, moved only by release-please. Before 1.0 any release may change the API and the ABI.
- **The ABI version** (`MRHI_ABI_VERSION` in `base.h`, made from the library version): the binary interface the headers describe, the whole version before 1.0 and the major and minor version from 1.0 on, with its top bit always set. `mrhiDefaultInstanceDef` is built in the program, so it stamps the instance def with the program's headers; `mrhiCreateInstance` checks the cookie first and the stamp second and refuses another release's with `mrhi_errorVersion` before it reads anything else, since such a def may have another layout. Every other def is used under an instance that passed, so it carries only its cookie.
- **The contract version** (`MRHI_CONTRACT_VERSION`): the revision of `contract/mrhi.json` the headers were generated from.
- **The SPI version** (`MRHI_SPI_VERSION` in `src/driver.h`): the driver interface's. Any change to the installed SPI headers raises it, and the conformance suite's mustpass list is renamed to it ([conformance.md](conformance.md)).
- **The container version** (1, in the container's header): any change to the container's bytes raises it ([shaders.md](shaders.md)).
- **The pipeline cache envelope's version**, beside the ABI version, driver and adapter it names: a cache of another envelope or ABI version is refused with `mrhi_errorVersion`, and one from another driver or adapter is reported stale; either is ignored.
