# mrhi-0024. Drivers built outside the tree

Status: Accepted

## Context

Console drivers are closed implementations of the driver SPI that can
never enter this repository. mrhi-0003 decided that drivers are
compiled in, since the web loads no code at run time, and that a
driver from outside is handed to the instance through a critical
chained struct; neither the struct nor installed SPI headers existed.
bgfx and SDL 3 admit their console renderers at source level, by
replacing a stub file or defining a private bootstrap and rebuilding
the library against its internal header; neither checks a version.
The requirements ask for a version and size handshake whose mismatch
is a typed failure, and for mustpass lists per SPI version.

## Decision

- `mrhiExternalDriverDef` (`mrhi_structExternalDriver`) on an instance
  def carries a driver's instance vtable and its own pointer, made by
  the driver's code before the instance. The instance starts it
  instead of the build's driver, owns it once created (destroying it at
  its end), and leaves it to the program when creation fails. It is
  refused beside the test driver or the Vulkan driver's structs. The
  core reports its adapters as `mrhi_driverExternal`.
- The handshake: a vtable of another `MRHI_SPI_VERSION` is
  `mrhi_errorVersion`; one smaller than the core knows, or lacking a
  function, is `mrhi_errorInvalid`. A device's vtable is checked when
  the device is made, and a refused device is destroyed through its
  first function, which `destroy` stays in every version.
- The SPI headers (`driver.h`, `command.h`, `reflection.h`,
  `container.h`) are installed as `maul-rhi/spi/`. Their types are the
  SPI; any change to them raises `MRHI_SPI_VERSION`, now 4, the first
  installed. The core functions they declare are not part of it.
- Mustpass lists of the conformance suite's named cases per SPI
  version follow in a later change.

## Consequences

A console driver is a library of its own, built against an install of
Maul RHI and linked into the program, with no file of this repository
replaced. A test builds such a driver against an install alone. The
shader container carries code for the four open APIs only; a driver
of another API needs its own code in containers, decided when one
exists.
