# Documentation

The [project README](../README.md) covers building and starting the command-line
runner. These guides describe the hardware model and its interfaces. Validation
notes identify tested behavior and remaining limits where available; some
implemented paths still lack cartridge coverage or hardware captures.

## Configuration and validation

Use [hardware configuration](hardware/configuration.md) to select region, RAM,
CIC, save hardware, controllers, and attached cartridges. [Persistent storage](hardware/storage.md)
explains the corresponding files and when they are written.

The [testing guide](testing.md) gives local validation commands and the pinned
cartridge-test build. The [cartridge fixture guide](testing/cartridge-fixtures.md)
describes source preparation and the records needed to reproduce its inputs.
[Continuous integration](testing/continuous-integration.md)
describes platform jobs and retained reports. Compare changes against the
[accuracy baseline](testing/accuracy-baseline.md) and
[recorded validation results](testing/validation-results.md), which identify the
inputs, completed runs, and unresolved failures. A passing default cartridge
does not establish that the extended groups passed.

[Compatibility captures](testing/compatibility-captures.md) explains how to
record repeatable video, audio, and execution evidence. The
[triangle fixture guide](testing/rdp-triangle-fixtures.md) explains the
experimental command-packing and coverage corrections.
[Cartridge build layout](testing/cartridge-build-layout.md) explains why timing
results from different test binaries need separate records.

## CPU, memory, and system clocks

[CPU timing](hardware/cpu-timing.md) covers instruction costs and memory waits.
[Instruction fetch ordering](hardware/instruction-fetch.md) explains fetches,
exceptions, and cache interactions. Separate guides cover
[floating-point execution](hardware/floating-point.md), [COP2](hardware/cop2.md),
and [CPU NMI entry](hardware/cpu-nmi.md).

[Physical bus decoding](hardware/physical-bus.md) describes memory windows and
unmapped accesses. [RDRAM interface state](hardware/rdram-interface.md) covers
chip registers, address mapping, hidden bits, and refresh.
[RCP event scheduling](hardware/rcp-scheduling.md) connects CPU clock advances,
DMA completion, interrupts, and output delivery.

## Signal processor

[RSP control flow](hardware/rsp-control-flow.md) covers scalar execution,
branches, and halt behavior. [RSP instruction timing](hardware/rsp-pipeline.md)
describes issue pairing, dependency waits, and branch bubbles.
[SP DMA](hardware/rsp-dma.md) covers transfer registers, row visibility, and
the clocks used by the scheduler.

## Display processor

Begin with [command streams](hardware/rdp-command-stream.md) for packet assembly,
DMA sources, status controls, and synchronization. The rendering guides follow
the data through the pipeline:

| Stage | Guides |
| --- | --- |
| Primitive setup | [Fill](hardware/rdp-fill.md), [copy](hardware/rdp-copy.md), [triangle interpolation](hardware/rdp-triangles.md) |
| Texture memory and sampling | [Loads](hardware/rdp-texture-loads.md), [sampling and textured rectangles](hardware/rdp-texture-sampling.md) |
| Pixel processing and storage | [Color processing](hardware/rdp-color.md), [depth and coverage](hardware/rdp-depth.md), [framebuffer formats](hardware/rdp-framebuffers.md) |

## Video, audio, and cartridge transfers

[Video timing](hardware/video-timing.md) explains VI counters and interrupt
boundaries; [field output](hardware/video-scanout.md) covers the sampled image.
[Audio timing](hardware/audio-timing.md) describes the AI FIFO, DAC clock, and
sample delivery.

[PI transfer timing](hardware/peripheral-interface.md) covers progressive DMA
and register visibility. [Cartridge bus transactions](hardware/cartridge-bus.md)
describes device selection and bus latches. Save-device guides cover
[SRAM](hardware/sram.md), [EEPROM](hardware/eeprom.md),
[FlashRAM](hardware/flash-memory.md), and the [cartridge clock](hardware/cartridge-rtc.md).

## Boot, controllers, and accessories

[PIF boot control](hardware/pif-boot.md) and [warm reset](hardware/warm-reset.md)
describe startup and the reset-button sequence. [The serial interface](hardware/serial-interface.md)
connects CPU and DMA transfers to [Joybus packets](hardware/joybus.md).

Accessory guides cover [Controller Pak](hardware/controller-pak.md),
[controller accessories and Rumble Pak](hardware/controller-accessories.md),
[Bio Sensor](hardware/bio-sensor.md), [mouse input](hardware/mouse.md),
[GameCube controllers](hardware/gamecube-controller.md), and
[Transfer Pak](hardware/transfer-pak.md). The
[Game Boy cartridge guide](hardware/game-boy-cartridge.md) describes the mapper
and clock behavior exposed through Transfer Pak.

## Finding the implementation

Public interfaces are under `include/cupid/`. Source and tests use subsystem
folders, with shared entry points and fixtures at the top level. The current
layout includes both top-level CPU/RSP files and their subsystem directories.

| Area | Implementation | Regression tests |
| --- | --- | --- |
| CPU and floating point | `src/cpu*.cpp`, `src/cpu/`, `src/fpu.cpp` | `tests/cpu/`, top-level CPU/FPU tests |
| Physical memory and peripherals | `src/bus*.cpp`, `src/bus/`, `src/rdram.cpp`, `src/rcp/` | `tests/rcp/`, top-level bus and memory tests |
| Signal processor | `src/rsp*.cpp`, `src/rsp/` | `tests/rsp/`, top-level RSP tests |
| Display and video | `src/rdp.cpp`, `src/rdp/`, `src/vi/`, `src/rcp/vi.cpp` | `tests/rdp/`, `tests/vi/` |
| Cartridge devices | `src/cartridge/` | `tests/cartridge/`, device cases in `tests/rcp/` |
| Runner and persistent storage | `src/main.cpp`, `src/host/`, `src/storage/` | `tests/host/` |

`tools/ci/validate.py` runs local CI. `tests/compatibility/` contains the capture
workflow. Check the relevant guide and its regression fixtures before changing
a hardware path; changes to a shared bus or scheduler can affect several rows
of this table.
