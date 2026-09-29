# mrhi-0002. The contract schema generates the public headers

Status: Accepted

## Context

The requirements make a machine-readable contract the source of truth
for the public headers, the driver SPI, the thread safety tables and
the mapping appendix skeletons, with drift a build failure. JSON keeps
the tools within Python's standard library.

## Decision

- **The contract** is `docs/contract/mrhi.json`, described in
  `docs/contract/README.md`. Its names are snake_case; the generator
  turns them into the family's C names.
- **The generator** is `tools/gen_contract.py`. It refuses a contract
  with structural errors (unknown kinds or types, repeated names or
  values, a result without success as zero, a missing doc, an unknown
  thread safety class) before writing anything.
- **Its outputs** are the public headers in `include/maul-rhi/` and
  `docs/contract/thread-safety.md`. They are written in the family's
  style and pass the format, documentation and source checks like
  hand-written code. Unlike `src/generated/` (conventions section 6), no
  rule is waived for them, because they are the API programs read.
- **Mappings:** every function, struct, enum and bitflags is either
  mapped onto Vulkan, D3D12, Metal and WebGPU or declared the library's
  own with the reason (`"library"`). Enums, bitflags and structs map
  per value or member, or whole; a function maps whole, one row per
  API. Each row is classed direct, emulated, restricted or
  absent-rejected with a note, and an emulated row states its cost.
  `docs/contract/mappings.md` shows them all. The generator refuses an
  unclassed concept in a header marked classed, and everywhere once the
  contract is.
- **Drift:** the gate and CI run `tools/gen_contract.py --check`, which
  fails on any generated file that differs from what the contract
  makes, including a hand edit.

## Consequences

A change to the API is an edit to the contract and a regeneration. A
header that disagrees with the contract cannot be merged, and the
thread safety table is always the headers' own.
