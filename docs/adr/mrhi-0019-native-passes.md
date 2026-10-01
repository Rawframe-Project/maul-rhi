# mrhi-0019. Native passes for vendor upscalers

Status: Accepted

## Context

FSR, DLSS (through Streamline), XeSS and MetalFX each record their work
into a command buffer the host gives them, reading and writing
resources whose native object and current layout or state they are
told, and they are made from the host's native device. On Vulkan, XeSS
and Streamline also need extensions and features enabled when the
device is made. The requirements ask for these through explicitly
per-API, explicitly gated interfaces, never a generic native-handle
call.

Every driver records a frame at its submission, from the core's command
stream, and the library takes no callbacks, so no native command buffer
of the library's exists while the program records a pass.

## Decision

- **A native pass** (`mrhiPassDef::native`) declares its accesses as
  any pass does, to imported resources only, with no targets, query
  sets or heap; every encoder call refuses it. While it is recorded the
  program hands it a native command buffer it recorded itself; on
  Vulkan, `mrhiSetVulkanPassCommands` takes a primary `VkCommandBuffer`
  from the program's own pool on the device's queue family. At
  submission the driver ends its own command buffer there, with every
  barrier into the pass ordered against all commands and memory
  access, submits the program's buffer next, and begins its next one
  with a barrier that orders everything after it behind the program's
  writes. A frame holds at most `MRHI_NATIVE_PASSES` of them.
- **States in a native pass** follow the access: on Vulkan,
  `SHADER_READ_ONLY_OPTIMAL` for sampled textures, `GENERAL` for
  storage, `TRANSFER_SRC_OPTIMAL` and `TRANSFER_DST_OPTIMAL` for copies.
  The program's commands leave every declared resource so.
- **Native objects**, borrowed for the device's life:
  `mrhiGetVulkanDevice` (instance, physical device, device and the
  library's `vkGetInstanceProcAddr` and `vkGetDeviceProcAddr`) and
  `mrhiGetVulkanTexture` (a device texture's image, memory, offset,
  format, usage and create flags).
- **Features at device creation**: `mrhiDeviceVulkanExtensions` carries
  a features chain whose core feature structs are merged into the
  library's; any other struct is refused, since the library cannot
  place a struct of unknown size.
- **Other drivers**: D3D12 and Metal follow with their own headers;
  until then, and on WebGPU, a native pass is refused as unsupported.

## Consequences

- An upscaler integrates as one native pass between the library's
  passes, with no handle crossing the portable API.
- A frame with native passes submits several command buffers in one
  submission, in order; each native pass costs a full barrier on each
  side.
- Feature structs beyond the core ones (an optical flow feature, say)
  wait for a change that knows their sizes.
