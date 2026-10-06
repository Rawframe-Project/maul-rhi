# mrhi-0027. Diagnostics: coded records in opt-in queues

Status: Accepted

## Context

The requirements ask that diagnostics go to an opt-in per-device
destination with a bounded queue. The family forbids callbacks into the
program, and the library only counted its refusals
(`mrhiGetInstanceMisuse`, `mrhiGetDeviceMisuse`): a program learned that
a call was invalid input, never which check refused it. D3D12's info
queue is a bounded queue the program reads; the Vulkan validation layers,
D3D12 and sokol_gfx give each check a stable number, so that programs
filter by identity and leave the text to people.

## Decision

- Each refusal of invalid input names its check with an
  `mrhiDiagnosticCode`, a closed enum generated from the contract;
  `mrhiDiagnosticText` returns the check's English sentence. Codes are
  only added, never renumbered.
- An instance and a device each keep a diagnostic queue of at most
  `diagnostics` records, a named limit in `mrhiInstanceLimits` and
  `mrhiDeviceLimits`, 0 by default (no queue). `mrhiNextInstanceDiagnostic`
  and `mrhiNextDeviceDiagnostic` take the oldest record.
- A record holds the code and a count: a refusal by the newest record's
  check adds to its count. A full queue drops new records, keeping the
  first ones; the misuse counters stay exact.
- In a build with the validation layer (mrhi-0025), each breach of the
  driver SPI it counts is also a record in the instance's queue, under
  a driver code naming the rule broken.
- Refusals come from any recording thread, so recording and taking hold
  a short lock.

## Consequences

A program that asks for a queue learns why each call failed without a
debugger or a log; one that does not pays a branch on the refusal path.
The texts and codes cost about 10 KiB of wasm (101 807 bytes against
the 131 072 budget, from 92 082). Some codes still cover a helper that
judges several conditions at once; finer codes can be added later
without breaking programs.
