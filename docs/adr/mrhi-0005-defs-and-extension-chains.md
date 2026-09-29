# mrhi-0005. Defs keep the family cookie and carry an extension chain

Status: Accepted

## Context

The family builds every object from a def whose cookie refuses an
uninitialized struct (conventions section 12). The contract extends
calls with chained structs, and requires an unknown critical extension
to be refused, never skipped.

## Decision

- **Two fields first:** every def opens with `cookie`, then `next`,
  the head of its extension chain. The Default function sets the cookie
  and leaves `next` NULL.
- **Chained structs** open with `mrhiChain`: `next` and a `type`.
- **Critical by default:** a type with bit 31 clear is critical, and
  one the library does not know is refused with
  `mrhi_errorUnsupported`. Bit 31 set marks a hint an older library may
  skip. A node without a type is invalid input.
- **Depth:** a chain is bounded by the instance's `chainDepth` limit,
  which also stops a cycle.

## Consequences

Each rule keeps what it protects: the cookie catches stack garbage and
the chain carries extensions, such as a driver's own structs. A hint
reaches an older library without failing it.
