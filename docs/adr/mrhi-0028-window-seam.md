# mrhi-0028. The Maul Window seam: no glue, a test-only check

Status: Accepted

## Context

Maul Window hands its windows to a GPU library as a native handle
bundle, `mwinNativeHandles`, once per surface generation; Maul RHI makes
surfaces from one chained source struct per window system. The two
were designed to meet field for field, but each library was tested
alone. A glue header would put a window type into Maul RHI's API or a
graphics header into Maul Window, which both libraries rule out.

## Decision

- No glue in either library. A program copies the bundle into the
  source struct of its platform (the guide, section 9, shows the copy),
  makes a new surface when `surfaceGeneration` changes, and configures
  again when the window's pixel size changes.
- `test/seam/`, built only with `MAUL_RHI_SEAM` (off by default),
  fetches Maul Window at a release tag and links it into the check
  alone; the library and its install never see it. The check makes a
  window, a surface from its bundle, presents, resizes the window,
  configures and presents at the new size, and ends the surface before
  the window.
- It runs on X11 under Xvfb with lavapipe in its own CI job and in the
  gate first; Win32 with D3D12 and the web canvas follow as their own
  slices. The tag moves by hand with each Maul Window release.

## Consequences

The seam both libraries promise is now exercised as a program uses it,
with no coupling between them: a program on SDL or GLFW fills the same
source struct the same way. A change to either shape fails the check
at the next tag update rather than in a program.
