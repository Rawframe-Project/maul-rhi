# Surfaces and presenting

A surface is a window's drawable area. Maul RHI makes one from the window's native handles and includes no window system header and links no window library: a program on Maul Window, SDL, GLFW or its own windows copies the handles into one source struct.

## Making a surface

`mrhiCreateSurface` takes a def with exactly one chained source:

| Source | Platform | Handles |
|---|---|---|
| `mrhiSurfaceSourceWin32` | Windows | the instance and window handles |
| `mrhiSurfaceSourceWayland` | Linux | the display and the surface |
| `mrhiSurfaceSourceXcb` | Linux, the BSDs | the connection and the window |
| `mrhiSurfaceSourceAndroid` | Android | the native window |
| `mrhiSurfaceSourceMetalLayer` | macOS, iOS | a `CAMetalLayer` |
| `mrhiSurfaceSourceCanvas` | the web | a canvas's selector |

A source the build's driver cannot use is `mrhi_errorUnsupported`. A surface lasts for one generation of the window's drawable: when the window system replaces it (Android's surface going and coming back, a canvas leaving the document), destroy the surface and make a new one. Destroying an instance destroys the surfaces left on it.

## What an adapter can do with it

`mrhiSurface_GetCaps` tells, per adapter, whether it presents to the surface at all, and if so:

- the colors it offers, preferred first, each a format with its primaries, transfer function and range given separately, every one a value the library lists: a color the driver reports with another value is left out, and an adapter left with no color does not present;
- the present modes, a set of `mrhiPresentMode` bits (`mrhi_presentFifoBit`, `mrhi_presentMailboxBit`, `mrhi_presentImmediateBit`), each 1 shifted left by its mode;
- the alpha modes, a set of `mrhiAlphaMode` bits (`mrhi_alphaOpaqueBit`, `mrhi_alphaPremultipliedBit`);
- the usages its images take;
- how sRGB output is reached.

Every presenting adapter offers fifo, opaque alpha, the render target usage, 8-bit sRGB in Rec. 709 (`mrhi_formatRgba8Unorm` or `mrhi_formatBgra8Unorm` with `mrhi_primariesBt709`, `mrhi_transferSrgb` and `mrhi_rangeStandard`) and one of the two twins below. An adapter whose driver reports caps without any one of these does not present. Surface formats are unorm, and sRGB is rendered through the format's sRGB twin, reached one of two ways, which the caps report. With `twinViews` the images stay unorm and the program renders through sRGB views of them; DXGI's flip model and WebGPU's canvases work this way. With `twinImages` the images are configured in the sRGB format itself; Android's swapchains work this way. A program that wants sRGB output reads the caps and names the twin as a view format or as the configured format.

`mrhiSuggestSurfaceColor` picks from the caps in a fixed order: the color asked for, then linear half floats in extended-range Rec. 709, then the first 8-bit sRGB color. The output may be the color asked for or one of the caps' colors, so one variable can be replaced with its suggestion, and a failure leaves it as it was. The program applies the suggestion or chooses otherwise.

## Configuring

`mrhiDevice_ConfigureSurface` configures a surface on a device with a color the caps report (or its twin, as above), its usages, a nonzero size, one present mode and one alpha mode. Anything the caps do not offer is refused, never quietly replaced.

- A surface is configured on one device at a time; another device finds it in `mrhi_errorState`.
- Configuring again reconfigures. A refused reconfiguration keeps the surface configured as it was. Only a platform call that fails after retiring the old swapchain leaves the surface unconfigured: Vulkan retires it whatever `vkCreateSwapchainKHR` answers, and D3D12 releases it first, as a window holds one swapchain.
- Where the window fixes its images' size (Vulkan on Win32 and X11), a size other than the window's is `mrhi_errorOutOfDate`: configure again at the window's size. Elsewhere the program's size is kept and the platform scales the images to the window.
- When the window's pixel size changes, configure again at the new size.
- `mrhiSurface_Unconfigure` ends a configuration. Destroying the surface or the device ends it too.

## Presenting

In a frame, `mrhiAcquireSurfaceImage` gives the next image as a frame resource. It answers:

- `mrhi_success` or `mrhi_suboptimal` (the window changed; configure again soon) with an image;
- `mrhi_occluded` without one, while the window is hidden or has no area;
- `mrhi_errorOutOfDate` without one: configure again before the next frame;
- `mrhi_errorDeviceLost`.

The same answer holds for the rest of the frame. The image begins undefined, so the frame writes it before reading it; a pass that writes it is never culled, and the frame leaves it ready to present. Submitting the frame presents every image it acquired, after its work. A dropped frame, or one whose submission fails, gives its images back. A device presents to any number of surfaces in one frame, each with its own present mode.

## Maul Window

Maul Window hands its windows to a GPU library as a bundle of native handles, `mwinNativeHandles`, once per surface generation. The copy into Maul RHI's source is the whole of the glue:

<!-- guide: not run: Maul Window's types; test/seam/seam.c runs the whole copy against Maul Window -->
```c
static const mrhiChain* SourceFrom(const mwinNativeHandles* handles, SeamSource* source)
{
    switch (handles->platform)
    {
    case mwin_platformX11:
        source->xcb = (mrhiSurfaceSourceXcb){
            .chain = {.type = mrhi_structSurfaceSourceXcb},
            .connection = handles->handles.x11.connection,
            .window = handles->handles.x11.window,
        };
        return &source->chain;
    case mwin_platformMacOS:
    case mwin_platformIOS:
        source->metal = (mrhiSurfaceSourceMetalLayer){
            .chain = {.type = mrhi_structSurfaceSourceMetalLayer},
            .layer = handles->handles.apple.layer,
        };
        return &source->chain;
    // Win32 (hinstance, hwnd), Wayland (display, surface), Android
    // (window) and the web (selector, selectorLength) copy the same way.
    default:
        return nullptr;
    }
}
```

`SeamSource` is a union of the source structs. When the bundle's `surfaceGeneration` changes, make a new surface; when the window's pixel size changes, configure again. `test/seam/seam.c` is the complete program, checked against a Maul Window release on X11, Wayland, Win32, macOS and the web.

## The web, on a worker

The WebGPU driver can run in a dedicated worker, which leaves the page's main thread free. The page transfers the canvas's control (`transferControlToOffscreen()`) and posts the `OffscreenCanvas` to the worker, which registers it in `Module.mrhiCanvases` under a name; a canvas source with that name as its selector makes the surface. Without Emscripten, `Module` is the object the page passes to `maulRhiImports`. A window library stays on the main thread, so the program forwards size changes to the worker, which resizes the `OffscreenCanvas` and configures the surface again.
