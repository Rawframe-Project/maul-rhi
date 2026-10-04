# mrhi-0026. The WebGPU driver in a worker

Status: Accepted

## Context

A browser runs WebGPU in a dedicated worker on an OffscreenCanvas,
which leaves the main thread to the page; the requirements ask that
this path stay possible. The WebGPU specification exposes
`navigator.gpu` to workers (`WorkerNavigator includes NavigatorGPU`)
and lets a `GPUCanvasContext` belong to an OffscreenCanvas. The WebGPU
driver touched the DOM in one place: a canvas surface was found with
`document.querySelector`, and a worker has no document.

## Decision

- A canvas source's selector first names a canvas the program put in
  `Module.mrhiCanvases` (names to OffscreenCanvases or canvas
  elements), then an element of the document, where there is one. In a
  worker the program registers the OffscreenCanvas that
  `transferControlToOffscreen()` gave it. The C API does not change.
- The web runner has a worker mode, in which the page transfers both
  canvases to a dedicated worker that runs the test, registers them
  under their selectors, and sends back its output, its status and the
  driver's WebGPU errors. The conformance suite runs that way too
  (`conformance_worker`), with Emscripten and without.
- Maul Window stays on the main thread; the program forwards size
  changes to the worker.

## Consequences

The registry lives on the module object both web builds already pass,
so the worker path costs one lookup. Every conformance case that runs
on the page runs in the worker, presenting to OffscreenCanvases.
