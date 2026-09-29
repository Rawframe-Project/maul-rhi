# mrhi-0009. Shader containers: both codes, one reflection, checked as hostile input

Status: Accepted

## Context

The library compiles no shaders at run time. Native drivers take
SPIR-V (translated for D3D12 and Metal), and the WebGPU driver takes
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
      with its location, scalar type, components and interpolation);
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
- **The writer**, `tools/mrhi_container.py`, standard library only,
  applies the same rules and refuses modules whose entry points or
  bindings disagree with the reflection.

## Consequences

Pipelines can be checked and laid out from the reflection alone,
identically on every API. A damaged or hostile container is refused as
invalid input, never read past its bounds. Every build carries both
codes, which costs size, not time.
