# mrhi-0001. Library profile

Status: Accepted

## Context

Every Maul library states in one record what its domain adds to the
family rulebook (family record 0005).

## Decision

- **Determinism:** GPU output is exempt from the family's rule of the
  same bits everywhere: drivers and hardware round, filter and blend
  differently. Everything the library decides itself is deterministic:
  validation, the frame graph's pass order, barriers and memory reuse
  for the same graph, and the answers to the same requests.
- **Threads:** none of its own (family record 0017). Pass encoders are
  externally synchronized, and different passes may record on
  different threads. Threads a GPU driver or the browser runs are the
  platform's.
- **Memory:** an instance and each device are created with the
  caller's allocator; a device owns its objects and its GPU memory,
  which the library suballocates itself, within named limits.
- **Platform dependencies:** the Vulkan loader, opened at run time, for
  the Vulkan driver, which compiles against the Khronos C headers kept
  as published in `khronos/`; the browser's WebGPU, called through
  `EM_JS` glue compiled into the library, on the web; Metal and
  Direct3D 12 later. Window handles come in as opaque
  pointers from the program; no window library is linked. The
  conformance suite links the XCB client library where it is
  installed, to make a window of its own.
- **Size:** the library with its WebGPU driver, built for the web at
  `-Oz` without its tests and linked with every public function kept,
  stays within 128 KiB of wasm. CI measures it on every commit
  (`tools/wasm_size.py`) and fails past the budget, which is lowered
  as the library allows. This is stricter than the family's rule of
  budgets checked at release (family record 0013), since the web build
  is where size costs most.
- **Commit areas:** `api`, `build`, `ci`, `container`, `conformance`,
  `docs`, `graph`, `samples`, `schema`, `tests`, `tools`, `vulkan`,
  `webgpu`.

## Consequences

A host sees every dependency per driver, knows that GPU results may
differ in their last bits between machines while the library's own
decisions do not, and knows that no thread runs the library's code
but the ones it calls it from.
