# mrhi-0018. External Vulkan objects for OpenXR

Status: Accepted

## Context

OpenXR's `XR_KHR_vulkan_enable2` has the runtime make the Vulkan
instance and device: `xrCreateVulkanInstanceKHR` and
`xrCreateVulkanDeviceKHR` take the application's create infos, add
the extensions and features the runtime needs, and call Vulkan
themselves. `xrGetVulkanGraphicsDevice2KHR` names the physical device
to use, and `XrGraphicsBindingVulkan2KHR` binds the session to the
instance, physical device, device, queue family and queue index. The
older `XR_KHR_vulkan_enable` lists the extensions instead, and the
application makes the objects.

Vulkan cannot say which extensions an instance or device was made
with, nor which features a device enabled. The driver's device
enables many (the floor, the granted features, descriptor indexing,
mutable descriptors) and they change with the def.

The library takes no callbacks, and no third-party code may enter it,
so it neither calls OpenXR nor asks the program mid-creation.

## Decision

`maul-rhi/vulkan.h` holds Vulkan-only chained structs and functions;
every other driver refuses the structs and answers the functions
`mrhi_errorUnsupported`.

- **Instances.** `mrhiInstanceVulkanAdopt` adopts a `VkInstance` with
  its `vkGetInstanceProcAddr`, its API version (1.3 or later) and the
  extensions it was made with. The driver reads its functions through
  that entry, opens no loader, uses surfaces and labels only where
  their extensions are named, and never destroys it.
  `mrhiInstanceVulkanExtensions` adds extensions to an instance the
  library makes. An instance that cannot be adopted or made is refused
  rather than left without a driver, since the program asked for it.
- **Devices.** `mrhiDescribeVulkanDevice` gives the
  `VkDeviceCreateInfo` and `VkPhysicalDevice` the driver would use for
  a device def, composed by the same code that opens devices
  (`vulkan_recipe.c`). The instance holds it until its next
  description or its end. A def carrying `mrhiDeviceVulkanAdopt` then
  adopts the device the runtime made from it: the def must match the
  description (the adapter, the features and the extra extensions),
  since a device made otherwise may lack what the driver uses. The
  library never destroys an adopted device.
  `mrhiDeviceVulkanExtensions` adds extensions to the device.
- **Session binding.** `mrhiGetVulkanPhysicalDevice` reads an
  adapter's physical device, to find the one the runtime names, and
  `mrhiGetVulkanQueue` the queue family and index a device submits on.
- **Swapchains** are enabled on a device only where the instance has
  surfaces, as `VK_KHR_swapchain` requires `VK_KHR_surface`.
- **Swapchain images.** `mrhiTextureVulkanAdopt`, chained on a
  texture def, adopts a `VkImage` as a texture; the def states its
  format, shape and usage, which Vulkan cannot report. OpenXR hands
  each image over in `COLOR_ATTACHMENT_OPTIMAL` (a color format) or
  `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` (a depth one) on the session's
  queue and takes it back the same way, so an adopted texture starts
  in the color or depth target state and every frame that uses it
  ends it there, whatever its last use, as a surface image ends ready
  to present. It must therefore be a render target, and it is never
  sealed. The library makes views of it and destroys neither it nor
  its memory.

## Consequences

- An OpenXR program describes the device, lets the runtime make it,
  and adopts it; the driver's device creation stays in one place.
- A program may use the instance and device before and after the
  library: the library's end leaves both alive, as the test checks.
- A runtime may add features and extensions the library does not
  know; the driver uses only its own, which the description named.
- A frame that renders to a swapchain image and then reads it pays one
  more barrier, back to the target layout, at its end.
