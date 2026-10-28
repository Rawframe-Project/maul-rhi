# Surfaces

How a window becomes something a device presents to, and the platform differences the surface rules absorb. The user page is [surfaces.md](../user/surfaces.md); each driver's swapchain details are on its page.

## One chained source per window system

Every graphics API makes its surface from a window system's handles: Vulkan through one extension per window system, DXGI from an HWND, Metal from a `CAMetalLayer`, WebGPU from a canvas. The library must work with any window library and depend on none, so a surface def carries exactly one chained source struct per window system, which the program fills by copying its window library's handles. No window system header is included; a window system added later is a new chained struct, not an API change. A source the driver cannot use is unsupported, and an outside driver's own sources use struct types with bit 30 set ([drivers.md](drivers.md#drivers-built-outside-the-tree)).

A surface lasts one generation of the window's drawable. When the platform replaces the drawable (Android destroys and recreates its surface; a canvas can leave the document), the program makes a new surface rather than the library tracking the change, because only the window library sees it happen.

## Capabilities and their floors

Formats, present modes and alpha modes differ by platform and by adapter, so they are reported per adapter (`mrhiGetSurfaceCaps`) and the program picks among what the platform reports rather than trusting one format. Every presenting adapter offers at least fifo, opaque alpha, the render target usage, 8-bit sRGB in Rec. 709, and one way to that format's twin.

Colors are reported with primaries, transfer function and range as separate fields, because the APIs name color spaces as combinations (DXGI's scRGB, Vulkan's color space and format pairs, Metal's color space names) that do not line up one to one.

**Why the twin.** Surface formats are unorm, and sRGB output goes through the format's sRGB twin, because neither way of reaching it is portable alone. DXGI's flip model and WebGPU's canvases take no sRGB buffer, only sRGB views of unorm buffers. Android's swapchain, which the platform implements for every device, offers no `VK_KHR_swapchain_mutable_format`, so it cannot give sRGB views, but it lists the sRGB formats themselves. The caps therefore say which way an adapter takes: `twinViews` or `twinImages`.

**The fallback order** of `mrhiSuggestSurfaceColor` is fixed: the color asked for, then half floats in linear Rec. 709 of extended range, then the first 8-bit sRGB color. It is a suggestion; the program applies it or chooses otherwise.

## Configuration rules

- A configuration names a reported color (or the twin, as the caps allow), view formats where twin views are reached, usages the surface reports and the format takes on the device, a nonzero size within the limits, one present mode and one alpha mode. Anything unsupported is refused, never replaced, so a program never presents in a format it did not ask for.
- A surface is configured on one device at a time; another device finds it in `mrhi_errorState`.
- Configuring again reconfigures. A driver hands back the old swapchain's handle when it refuses before its platform call, and the configuration stays as it was. Once the platform call has run, the old swapchain is retired, even when the call fails, and a failure leaves the surface unconfigured. Vulkan retires the old swapchain whatever `vkCreateSwapchainKHR` answers, so its driver checks the slot, the caps, the size and the format first. D3D12 releases the old swapchain before `CreateSwapChainForHwnd`, as a window holds one, and nothing it checks alone comes before that. Metal allocates the new record before it lets the old one go.
- Where the window fixes its images' size (Vulkan on Win32 and X11), a size other than the window's is `mrhi_errorOutOfDate`. Android reports the window's size yet takes any size within the surface's bounds and scales it, so the driver accepts any such size there. Elsewhere the program's size is its own and the platform scales.
- Destroying a surface first ends its configuration; destroying a device ends every configuration it holds; destroying an instance destroys the surfaces left.

## Acquiring and presenting

`mrhiAcquireSurfaceImage`, while a frame is declared, answers once for the frame: an image with `mrhi_success` or `mrhi_suboptimal`, or no image with `mrhi_occluded`, `mrhi_errorOutOfDate` or `mrhi_errorDeviceLost`. The same answer holds for the rest of the frame, so a frame that asks twice sees one decision. The image is a frame resource of the surface's configuration, tracked and never aliased; it begins undefined and ends in the present state ([frame-graph.md](frame-graph.md#imported-sealed-and-presented-states)).

The driver presents every image of a submitted frame after its work. A dropped frame, one whose submission fails, and one still open when its device is destroyed give their images back; an image given back stays acquired by the driver and is handed out again at the next acquire. A surface whose image the open frame holds cannot be configured again until the frame ends.

## Maul Window, and no glue

Maul Window hands its windows to a GPU library as a native handle bundle once per surface generation, and Maul RHI makes surfaces from one source struct per window system. The two were designed to meet field for field. A glue header would put a window type into Maul RHI's API or a graphics type into Maul Window, which both libraries rule out, so there is none: a program copies the bundle into the source of its platform. A program on SDL or GLFW fills the same struct the same way.

The copy is exercised as a program uses it by `test/seam/`, built only with `MAUL_RHI_SEAM`, which fetches Maul Window at a release tag and links it into the check alone; the library and its install never see it. The check makes a window, a surface from its bundle, presents, resizes the window, configures and presents at the new size, and ends the surface before the window. Its frames never wait for the GPU: each takes the answers that came and submits when the device has room, as a program on the browser's frames must, so one check runs everywhere: X11 under Xvfb and Wayland under weston's headless compositor with lavapipe, Win32 with WARP, the web with Maul Window's canvas, and macOS with Maul Window's layer. The tag moves by hand with each Maul Window release, so a change to either shape fails the check at the next update rather than in a program.

## Workers on the web

The WebGPU driver touched the DOM in one place: a canvas source's selector was resolved with `document.querySelector`, and a worker has no document. A selector now first names a canvas the program registered in `Module.mrhiCanvases` (names to `OffscreenCanvas`es or canvas elements), then an element of the document where there is one. The C API did not change, and the registry lives on the module object both web builds already pass, so the worker path costs one lookup. Window libraries stay on the main thread; the program forwards size changes.
