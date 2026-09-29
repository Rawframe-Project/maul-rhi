# mrhi-0009. Shader containers: both codes, one reflection, checked as hostile input

Status: Accepted

## Context

The library compiles and translates no shaders at run time. Vulkan
takes SPIR-V, D3D12 DXIL, Metal a metallib, and the WebGPU driver takes
WGSL. Pipelines need each entry point's interface and every binding's
kind to make their layouts, and WebGPU needs them explicitly, in its
own terms. A program loads shader bytes from disk or the network, so
they are untrusted.

## Decision

- **One container** holds, made offline:
  - a SPIR-V module and a WGSL module, each with every entry point;
  - one reflection, in WebGPU's bind group layout terms, since it is
    the strictest consumer and the other APIs derive their layouts from
    the same facts:
    - the entry points: stage, name, workgroup size and storage, the
      fragment builtins that constrain pipelines, and their interface
      variables (vertex inputs, color outputs, and the inter-stage
      variables a vertex entry writes and a fragment entry reads, each
      with its location, scalar type, components and interpolation),
      and the heaps it reads (added by mrhi-0015, whose containers
      carry no WGSL until WGSL reads heaps);
    - the bindings: table, slot, stages, kind and its details;
    - the root block's size;
    - the specialization constants, and whether each has a default.

    These are every fact WebGPU's pipeline validation reads from a
    shader, so the core can refuse any pipeline WebGPU would.
- **The layout** (`docs/contract/container.md`):
  - a little-endian header with a version;
  - a table of sections, 8-byte aligned and never overlapping;
  - sections of fixed-size records.

  Unknown sections are skipped, so a later writer can add one. Another
  version is `mrhi_errorVersion`.
- **A SHA-256 digest** of everything after it identifies the container
  (pipeline cache keys, matching a cook's output) and catches damage.
  It is standard, small in C, and in Python's standard library.
- **The reader** (`mrhiCreateShader`) checks every byte as hostile
  input before anything is kept: bounds, overlaps, enums, reserved
  zeros, UTF-8 names, unique names, slots, locations and ids, WGSL's
  interpolation rules, and WebGPU's rules for each binding. 16-bit
  floats need the device's `shaderF16`. It reads byte by byte, so nothing
  depends on the host's byte order. The device keeps the decoded
  reflection in one allocation. Drivers check the code itself.
  `fuzz_container` (built with `MAUL_RHI_FUZZ`) fuzzes the reader from
  the conformance suite's container, which `tools/container_seed.py`
  writes out. It sets each input's size field and digest before the
  parse, so that mutations reach the sections and records, and also
  parses each input cut short at its end; a container that parses
  must keep its parts inside its bytes and apart, and decode into a
  reflection that finds every entry and frees what it took. CI runs it
  for a minute on every push.
- **Native codes:** the D3D12 and Metal drivers read DXIL and Metal
  sections, made offline beside the SPIR-V and added as new section
  types when those drivers land; readers already skip section types
  they do not know. A container without the section its driver needs
  is refused as unsupported.
- **Metal code** (amended for the Metal driver): a Metal map, and MSL,
  a metallib or both, for containers using no heap. Metal takes a
  metallib, which only Apple's toolchain makes, or Metal Shading
  Language, which Metal compiles itself; the MSL lets a cook without
  that toolchain target Metal, while the library still translates
  nothing, and the driver prefers the metallib. SPIRV-Cross writes one
  entry per source, so each entry has its own MSL. The map records
  where the root block and each binding lie, a buffer, texture or
  sampler index, so a cook other than `tools/mrhi_msl.py` may place
  them its own way; the reader checks the indices against Metal's
  ranges and each other.
- **The writer**, `tools/mrhi_container.py`, standard library only,
  applies the same rules and refuses modules whose entry points or
  bindings disagree with the reflection.

## Consequences

Pipelines can be checked and laid out from the reflection alone,
identically on every API. A damaged or hostile container is refused as
invalid input, never read past its bounds. Every build carries both
codes, which costs size, not time.
