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
| `headers` | The public headers, in order, each with a `name`, a `doc`, an optional `preamble` and its `items`. |

A header's `preamble` lists the fixed blocks it opens with: `version`
(the version macros), `api` (the export macro) and `nodiscard`.

## Items

Every item has a `kind`, a snake_case `name` and a `doc`.

| Kind | Fields | C form |
|---|---|---|
| `result` | `values`: `name`, `value`, `doc` | a fixed-width `int32_t` type and its values; `success` is zero |
| `struct` | `members`: `name`, `type`, optional `doc` | a typedef struct |
| `function` | `args` (`name`, `type`, `doc`), optional `returns` (`type`, `doc`), `thread_safety` | a documented declaration; one returning `result` is nodiscard |

C names follow `docs/conventions.md` section 4: `get_version` becomes
`mrhiGetVersion`, the struct `version` becomes `mrhiVersion`, the value
`error_invalid` becomes `mrhi_errorInvalid`, and a member or argument
`view_count` becomes `viewCount`.

## Types

A type is a primitive (`bool`, `int8` to `int64`, `uint8` to `uint64`,
`size`, `float32`, `float64`, `static_cstring`), `result`, or a
reference to an item as `kind.name`, such as `struct.version`.

## Thread safety

`thread_safety` has a `class` and, where it applies, an `object` and a
`note`:

| Class | Opening |
|---|---|
| `any` | Safe from any thread. |
| `any_exclusive` | Safe from any thread; `object` is used by one thread at a time. |
| `main` | Main thread only. |
| `realtime` | Real-time safe: no allocation, lock or wait. |
