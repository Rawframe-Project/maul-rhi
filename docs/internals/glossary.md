# Glossary

The words the code, the documentation and the tests use with one meaning.

- **Access.** One use a pass declares of a resource: its kind (sampled, storage read or write, copy source, indirect arguments and so on) and the mips, layers and planes it covers. The frame graph plans barriers from accesses alone.
- **Adapter.** A GPU a driver found, named by an id that stays the same across searches while the GPU is there.
- **Arena.** The device's per-frame memory for recorded commands, `frameCommandBytes`, taken in 4 KiB chunks.
- **Class (of a mapping).** How an API provides a concept: direct, emulated (at a stated cost), restricted (on some devices or under conditions), or absent-rejected (never, so the core refuses it on that API).
- **Class (of a pass).** Graphics, async compute or transfer. Passes of one class run in declaration order.
- **Compile.** The step between declaring a frame and recording it: culling, store choice, barrier planning and transient placement.
- **Container.** The offline-made file holding a set of shader entry points in every API's code, with one reflection.
- **Contract.** `contract/mrhi.json`, the public API as data, from which the headers are generated.
- **Cookie.** The constant a `mrhiDefault...Def` function puts first in a def, so the library refuses a def nobody initialized.
- **Core.** Everything above the driver SPI: the code that decides.
- **Culled.** A pass the compile dropped because nothing reads what it writes. It is never recorded.
- **Def.** The struct a creation or a frame declaration takes, made by its default function.
- **Driver.** One implementation of the SPI: Vulkan, D3D12, Metal, WebGPU, the test driver, or one built outside the tree.
- **Floor.** What every listed adapter offers without being asked: WebGPU's default limits and a fixed set of features.
- **Frame.** One begin, declare, compile, record and submit cycle on a device; the only way work reaches the GPU.
- **Frame view.** The read-only description of a submitted frame a driver reads.
- **Handle.** A driver's 64-bit name for an object it made; zero is invalid. The core maps its ids onto handles.
- **Heap.** A bindless table of resource and sampler entries at indices the program chooses.
- **Imported.** A device object joined to a frame; tracked, never aliased, carrying its state between frames.
- **Integration test.** A test that drives a real GPU, window or browser, labeled `integration`.
- **Kept.** A pass the compile did not cull.
- **Mapping.** A concept's row for one API in the contract: its class and a note on how.
- **Misuse.** A count on an instance or device of the calls it refused as invalid.
- **Native pass.** A pass whose commands the program records into a native command buffer of its own.
- **Notification.** A record in an owner's queue answering a request.
- **Plan.** What the compile produced: the barriers, each resource's lifetime and place, each pass's stores.
- **Poll.** The core asking a driver for finished work; the only way a driver's answers reach the core.
- **Request.** An operation that finishes later, named by a request id and answered by exactly one notification.
- **Root block.** A few bytes of constants per pass, the cheapest binding every API has.
- **Sealed.** A resource left in a read-only state that any pass may read through a heap without declaring it.
- **SPI.** The driver interface between the core and a driver (`src/driver.h`), with its own version.
- **Test driver.** The driver without a GPU, for tests: it answers at the next poll and walks every frame.
- **Transient.** A resource declared by a frame, living for that frame only, placed by lifetime in the frame's memory.
- **Twin.** A format's sRGB or linear counterpart, the one view format a texture may add.
- **Validation layer.** The optional wrapper around a driver that checks what the driver answers.
