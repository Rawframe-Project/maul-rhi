# Native interop

The portable API holds no native handle. Where a program needs one, for OpenXR or a vendor upscaler, a per-API header gives it: `maul-rhi/vulkan.h`, `maul-rhi/d3d12.h` and `maul-rhi/metal.h`. Every other driver refuses their chained structs and answers their functions `mrhi_errorUnsupported`, so a program can call them unconditionally and branch on the result.

## OpenXR on Vulkan

`XR_KHR_vulkan_enable2` has the runtime make the Vulkan instance and device from the application's create infos, adding what it needs. Vulkan cannot report afterwards which extensions or features an object was made with, so Maul RHI describes what it would make and adopts what the runtime made from that description:

1. **The instance.** Make the `VkInstance` with the runtime, then chain `mrhiInstanceVulkanAdopt` on the instance def with the instance, its `vkGetInstanceProcAddr`, its API version (1.3 or later) and the extensions it was made with. The driver reads every function through that entry, opens no loader of its own, and uses surfaces and labels only where their extensions were named. `mrhiInstanceVulkanExtensions` instead adds extensions to an instance the library makes.
2. **The physical device.** `mrhiAdapter_GetVulkanPhysicalDevice` gives each adapter's `VkPhysicalDevice`, to find the one `xrGetVulkanGraphicsDevice2KHR` names.
3. **The device.** `mrhiInstance_DescribeVulkanDevice` gives the `VkDeviceCreateInfo` and `VkPhysicalDevice` the driver would use for a device def; hand them to `xrCreateVulkanDeviceKHR`. Then chain `mrhiDeviceVulkanAdopt` with the new `VkDevice` on the same def: the def must match the description (the adapter, the features and the extra extensions), since a device made otherwise may lack what the driver uses.
4. **The session.** `mrhiDevice_GetVulkanQueue` gives the queue family and index the device submits on, for `XrGraphicsBindingVulkan2KHR`.
5. **Swapchain images.** Chain `mrhiTextureVulkanAdopt` on a texture def to adopt each `VkImage` as a texture; the def states its format, shape and usage, which Vulkan cannot report. OpenXR hands images over and takes them back in the color or depth attachment layout, so an adopted texture must be a render target and every frame that uses it leaves it in that layout, as a surface image is left ready to present. It is never sealed.

The library never destroys an adopted instance, device or image: the program may use them before and after it.

## Vendor upscalers

FSR, DLSS (through Streamline), XeSS and MetalFX record their work into a command buffer the host gives them, reading and writing resources whose native objects and current layouts they are told. Maul RHI records every frame at submission, so it has no command buffer of its own open while a pass records. A **native pass** bridges the two:

- Set `native` on the pass def. A native pass declares its accesses as any pass does, to imported resources only, with no targets, query sets or heap. Every encoder call refuses it.
- While recording, hand it a command buffer the program recorded itself: `mrhiPass_SetVulkanCommands` (a primary `VkCommandBuffer` from the program's own pool on the device's queue family), `mrhiPass_SetD3d12Commands` (a closed direct `ID3D12GraphicsCommandList`) or `mrhiPass_SetMetalCommands` (an `MTLCommandBuffer` from the device's queue, with no encoder open, not committed). WebGPU refuses native passes.
- At submission the driver runs the program's commands between its own, with every declared resource in its access's state: on Vulkan, `SHADER_READ_ONLY_OPTIMAL` for sampled textures, `GENERAL` for storage, `TRANSFER_SRC_OPTIMAL` and `TRANSFER_DST_OPTIMAL` for copies; on Direct3D 12 the matching resource states, with buffers in the common state; Metal tracks the hazards itself. The program's commands must leave every declared resource in the same state.
- A frame holds at most `MRHI_MAX_NATIVE_PASSES` native passes, and each costs a full barrier on each side.

The native objects an upscaler is made from are borrowed for the device's life: `mrhiDevice_GetVulkanDevice` (the instance, physical device, device and the library's `vkGetInstanceProcAddr` and `vkGetDeviceProcAddr`), `mrhiDevice_GetD3d12Device` (the device and its queue), `mrhiDevice_GetMetalDevice` (the device and its queue), and `mrhiTexture_GetVulkanTexture`, `mrhiTexture_GetD3d12Texture` and `mrhiTexture_GetMetalTexture` for a device texture's native object.

On Vulkan, XeSS and Streamline need extensions and features enabled when the device is made: `mrhiDeviceVulkanExtensions` on the device def adds the extensions, and a features chain whose core Vulkan feature structs are merged into the library's own. Any other struct in that chain is refused, since the library cannot place a struct whose size it does not know.
