# mrhi-0007. Surfaces from one chained native source, configured on a device

Status: Accepted

## Context

Every graphics API makes its surface from a window system's handles:
- Vulkan through one extension per window system;
- DXGI from an HWND;
- Metal from a `CAMetalLayer`;
- WebGPU from a chained source.

Formats, present modes and alpha modes differ per platform and per
adapter. The library must stay usable without any window library, and
must not make one a dependency.

## Decision

- **Surfaces** are instance ids made by `mrhiCreateSurface` from a
  def with exactly one chained source:
  - `mrhiSurfaceSourceWin32`;
  - `Wayland`;
  - `Xcb`;
  - `Android`;
  - `MetalLayer`;
  - `Canvas`.

  A source the driver cannot use is unsupported. A program copies its
  window library's handles into the source, so no window system header
  is included. When a window's surface generation ends, the program
  makes a new surface.
- **Capabilities per adapter** (`mrhiGetSurfaceCaps`):
  - whether the adapter presents there;
  - the color combinations, preferred first;
  - the present modes;
  - the alpha modes;
  - the usages;
  - how the sRGB twin is reached: as a view of the images
    (`twinViews`), or as the images' own format (`twinImages`).

  The floors are `fifo`, opaque alpha, the render target usage, 8-bit
  sRGB in Rec. 709 and at least one way to its twin. Formats are unorm,
  and sRGB is rendered through the twin. Neither way is portable alone:
  DXGI's flip model and WebGPU's canvases take no sRGB buffer, only
  sRGB views (twin views); Android's swapchain, which the platform
  implements for every device, offers no
  `VK_KHR_swapchain_mutable_format` but lists the sRGB formats (twin
  images, mrhi-0017).
- **The fallback order** (`mrhiSuggestSurfaceColor`): from the caps,
  the library suggests the color asked for, then half floats in linear
  Rec. 709 of extended range, then the first 8-bit sRGB color. The
  program applies the suggestion or chooses otherwise.
- **Searches:** an adapter request may name a compatible surface, and
  only the adapters that present to it are listed. The search answers
  `mrhi_errorStale` if the surface ends first.
- **Configuration** (`mrhiConfigureSurface`, `mrhiUnconfigureSurface`):
  a device configures a surface with a reported color, or the twin of
  its format where the surface takes twin images, view formats (the
  twin) where it takes twin views, usages the surface reports and the
  format takes on the device, a nonzero size within its limits, one present mode and one
  alpha mode. Anything unsupported is refused, never replaced.
  - A surface is configured on one device at a time; another device
    finds it in `mrhi_errorState`.
  - Configuring again reconfigures, and a failed reconfiguration
    leaves the surface unconfigured (Vulkan retires the old swapchain
    either way).
  - Where the window fixes its images' size (Vulkan on Win32 and
    X11), a size other than the window's is `mrhi_errorOutOfDate`: the
    program configures again with the window's size. Android reports
    the window's size yet takes any size within the surface's bounds,
    which its compositor scales to the window (mrhi-0017).
- **Lifetimes:**
  - destroying a surface first ends its configuration;
  - destroying a device ends every configuration it holds;
  - destroying an instance destroys the surfaces left.

## Consequences

One surface shape serves every window system, including ones added
later as new chained structs, and every window library. Programs pick
among what the platform reports rather than trusting one format.
