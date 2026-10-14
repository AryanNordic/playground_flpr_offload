# FLPR offload template

This sample moves a workload from the M33 to the FLPR on the nRF54LM20 DK and
measures the result. The FLPR sets up the peripheral, reads it, processes the
data and sends the result to the M33. The M33 forwards the result, which is where
your radio send goes. The same source also builds with the workload on the M33,
so you always have a baseline to compare against.

The two-image build, the inter-core transport, the result path and the
measurement are already in place. What you bring is the workload.

Four example peripheral folders are included: the SAADC, a LIS2DH12
accelerometer on TWIM, an ADXL362 accelerometer on SPIM, and `compute_only`, for
computation on data already in memory with no peripheral at all. The same
structure takes a UART, PWM or any other peripheral.

Board `nrf54lm20dk/nrf54lm20b/cpuapp`, NCS v3.3.4, one FLPR. This template has
not run on a board yet; see [Known limits](#known-limits).

## The workload is three functions

The contract is [common/workload.h](common/workload.h):

```c
int    workload_init(void);                                          /* set up the peripheral */
size_t workload_acquire(void *raw);                                  /* read one batch        */
size_t workload_process(const void *raw, size_t raw_len, void *out); /* compute the result    */
```

The interrupt trigger adds two more: `workload_wait()` blocks until the
peripheral has a batch, and `workload_stop()` stops the peripheral at the end of
a run.

```
offloaded   FLPR: init -> acquire -> process -> result  --IPC-->  M33: app_transmit() (radio)
baseline    M33:  init -> acquire -> process -> result ---------->      app_transmit() (radio)
```

In the offloaded build all three functions run on the FLPR, and the M33 image
does not contain them at all. In the baseline build all three run on the M33.
You write each function once, and the build decides which core runs it.

Moving all three is how this template is wired, not a hardware limit. The IPC
works in both directions, so a design that keeps init and acquire on the M33 and
sends raw batches to the FLPR for processing is possible. This sample does not
implement that path.

### Where the code goes

Your code is in two places: [app/](app/) and the peripheral folder you build
with.

| Code | File | Runs on |
| --- | --- | --- |
| `workload_process()`, your processing | [app/workload_process.c](app/workload_process.c) | FLPR (offloaded), M33 (baseline) |
| `app_transmit()`, what happens to each result: your radio send | [app/app_transmit.c](app/app_transmit.c) | M33, in both builds |
| The layout of one result, shared by the two files above | [app/result.h](app/result.h) | Both |
| `workload_init()`, `workload_acquire()`, and for the interrupt trigger `workload_wait()`, `workload_stop()` | `peripherals/<name>/` | FLPR (offloaded), M33 (baseline) |

Everything else (`src/`, `remote/`, `common/`) is the transport, the loop and
the measurement, and does not change with your workload.

How much of that you write depends on your peripheral:

- One of the included peripherals: init and acquire exist for both triggers,
  so you write `workload_process()` and `app_transmit()`.
- Another accelerometer with a Zephyr sensor driver: a devicetree overlay and
  conf files, no C code for init and acquire.
- Any other peripheral: you write init and acquire (plus wait and stop for the
  interrupt trigger), and process.
- No peripheral: start from `compute_only`, put your input in its
  `workload_acquire()`, and write process.

`workload_process()` returns the number of result bytes. Returning 0 sends
nothing for that batch, so the M33 is not involved at all: useful when the FLPR
only reports events.

`app_transmit()` runs in a normal M33 thread and may block. In the offloaded
build the FLPR keeps working meanwhile, and up to 8 results wait in a queue.
Beyond that results are dropped and the M33 log says how many.

Keep the work integer or fixed point. The FLPR has no FPU, so floating point is
emulated in software there and can be slower than on the M33.

## Try it

You need NCS v3.3.4 and an nRF54LM20 DK. From the sample folder:

```
west build -p -b nrf54lm20dk/nrf54lm20b/cpuapp --sysbuild .
west flash
```

This builds and flashes both images with the defaults: SAADC on AIN1 (P1.31,
nothing to connect), timer trigger, one batch every 100 ms. The M33 console
(uart20) prints one decoded result and one load line per second:

```
<inf> host: FLPR offload sample: workload on FLPR, peripheral saadc, timer trigger
<inf> host: FLPR started
<inf> app: result 0 ch0: min ... max ... mean ..., n 16
<inf> host: M33 active X.Y% | N.N results/s | FLPR per batch: acquire A.A us, process P.P us
```

The FLPR logs to its own console, uart30. It is only needed when something goes
wrong on the FLPR side.

For the baseline, build the same thing with the workload on the M33:

```
west build -p -b nrf54lm20dk/nrf54lm20b/cpuapp --sysbuild . -- -DFILE_SUFFIX=local
west flash
```

```
<inf> host: FLPR offload sample: workload on M33, peripheral saadc, timer trigger
<inf> app: result 0 ch0: min ... max ... mean ..., n 16
<inf> host: M33 active X.Y% | N.N results/s | M33 per batch: acquire A.A us, process P.P us
```

What the load line means:

- `M33 active` is the share of the last second the M33 was not idle. This is
  the number offloading changes. Zephyr attributes time at context switches, so
  an interrupt that arrives while the M33 is idle counts as idle. Work done
  entirely in interrupt handlers is under-reported, in both builds.
- `results/s` is the rate at which results reach `app_transmit()`. With the
  timer trigger it is just under 10 in both builds (100 ms sleep plus the batch
  itself).
- `acquire` and `process` are the average time per batch in
  `workload_acquire()` and `workload_process()`, timed by the same loop on
  whichever core runs it, so the FLPR and M33 figures compare directly. Each
  batch is timed with the 1 MHz system timer, so the resolution is 1 us.

The rate and the times count batches that produced a result. Batches for which
`workload_process()` returned 0 do not appear in them.

The example processing is deliberately small: a min, max and mean per channel
over 16 SAADC samples, and the Zephyr ADC read sleeps while each conversion
runs. At 10 batches a second it barely loads either core, so expect a low M33
figure in both builds. The difference appears when `workload_process()` does
real work: in the baseline the M33 active share grows with it, while in the
offloaded build it stays at the cost of receiving and forwarding. So the
comparison that tells you something is your own processing at your own rate.

The example `app_transmit()` logs one result per second, not every result, so
the log does not become the M33 load at high rates. Your send replaces it.

The build options for the run, set on the M33 image (in the offloaded build the
M33 passes them to the FLPR):

- `CONFIG_APP_SAMPLE_INTERVAL_MS`: delay between batches, default 100, 0 runs
  back to back. For example `-DCONFIG_APP_SAMPLE_INTERVAL_MS=20`.
- `CONFIG_APP_ITERATIONS`: batches to run, default 0 (continuous).
- `CONFIG_APP_REPORT_INTERVAL_MS`: how often the load line and the example
  result line print, default 1000.

No measured rates or percentages are given here, because none have been measured
on a board yet.

## Pick the peripheral and the trigger

Two build options, the same for the offloaded and the baseline build:

```
west build -p -b nrf54lm20dk/nrf54lm20b/cpuapp --sysbuild . -- \
        -DOFFLOAD_PERIPHERAL=twim_lis2dh -DOFFLOAD_TRIGGER=interrupt
```

| `OFFLOAD_PERIPHERAL` | Device | Wiring on the DK |
| --- | --- | --- |
| `saadc` (default) | SAADC, channel AIN1 | P1.31. Nothing to connect. |
| `twim_lis2dh` | LIS2DH12 on i2c21 (TWIM), address 0x19 | SDA P1.11, SCL P1.12, INT1 P1.13 |
| `spim_adxl362` | ADXL362 on spi22 (SPIM) | SCK P3.3, MOSI P3.0, MISO P3.1, CS P3.2, INT1 P1.14 |
| `compute_only` | None, a fixed ramp stands in for your data | Nothing. Timer trigger only. |

`OFFLOAD_TRIGGER` is `timer` (default) or `interrupt`. Add `-DFILE_SUFFIX=local`
to any combination to get its baseline. A name that does not exist, or a trigger
a folder does not support, stops the build with a message that says so.

Both sensors have in-tree Zephyr drivers and are common on Nordic reference
hardware (LIS2DH12 on the Thingy:52, ADXL362 on the Thingy:91 and Thingy:53).
Both run at 100 Hz here, and the example logs x, y and z as ch0, ch1 and ch2 in
cm/s². The I2C pins are the ones the nPM13xx samples use for TWIM on this DK,
and the SPI pins are the DK's expansion SPI. The two INT1 pins were picked as
free GPIOs and have not been checked against the DK schematic; move them if they
clash with your setup.

`compute_only` computes the same values for the same result number on both
cores, which makes it the simplest way to check that the offloaded and the
baseline builds agree.

Batch rates by combination:

| Peripheral | Timer | Interrupt |
| --- | --- | --- |
| SAADC | just under 10/s, 16 samples each | 50/s, 160 samples each (SAADC sampling at about 8 kHz) |
| Accelerometers | just under 10/s, one x, y, z sample each | 6.25/s, 16 samples each (100 Hz) |
| `compute_only` | just under 10/s, 64 samples each | not supported |

## Add your own peripheral

A peripheral is a folder under [peripherals/](peripherals/), and everything
specific to it lives there:

| File | What it holds | Applied to |
| --- | --- | --- |
| `peripheral.overlay` | The node, its pins, the `offload-peripheral` alias | The core that runs the workload |
| `hand_to_flpr.overlay` | The same node marked `reserved` | The M33, offloaded build only |
| `poll.conf`, `irq.conf` | The driver Kconfig for each trigger | The core that runs the workload |
| `<name>_poll.c`, `<name>_irq.c` | `init` and `acquire`, plus `wait` and `stop` for the interrupt trigger | The core that runs the workload |

[sysbuild.cmake](sysbuild.cmake) sends each file to the right image, so the
overlay that enables the peripheral on the FLPR is the same one that enables it
on the M33 in the baseline build.

To add one:

1. Copy the closest folder: `saadc` for a peripheral with its own C code,
   `twim_lis2dh` or `spim_adxl362` for a sensor on the Zephyr sensor API,
   `compute_only` for no peripheral.
2. Rename the folder, and rename the C files to match it (`<folder>_poll.c`,
   `<folder>_irq.c`). The build finds them by name.
3. Edit the overlays and conf files for your device, and the C files if there
   are any.
4. Build with `-DOFFLOAD_PERIPHERAL=<folder>`.

Nothing in `src/`, `remote/` or `common/` changes. If a file the build needs is
missing, the build stops and names it. A trigger you do not use does not need its
conf or C file.

The two accelerometer folders have no C file. They use the shared backend
[peripherals/shared/sensor_accel.c](peripherals/shared/sensor_accel.c), switched
on by `CONFIG_APP_SENSOR_ACCEL=y` in their conf files. The Zephyr sensor API
hides both the sensor model and the bus, so that code is identical over TWIM and
SPIM.

The example processing reads the raw batch as interleaved `int16_t` channels.
`CONFIG_APP_RAW_CHANNELS` in the folder's conf files says how many: 1 by
default, 3 in the accelerometer folders. Your own processing does not have to
use it.

The result is opaque bytes. Only `workload_process()` and `app_transmit()` need
to agree on its layout, and for the example that layout is
[app/result.h](app/result.h). The size limits are in
[common/workload.h](common/workload.h): `WORKLOAD_RAW_MAX` (320 bytes) for one raw
batch and `WORKLOAD_RESULT_MAX` (32 bytes) for one result. Both can be raised.
`WORKLOAD_RESULT_MAX` sizes the IPC message, and the shared buffers are 2 KB in
each direction. A result layout that does not fit stops the build.

## Two ways to trigger a batch

Timer (default). The core sleeps for the interval, reads a batch, processes it
and sends it. No peripheral interrupt is involved.

Interrupt. The peripheral sets the pace and the core sleeps until a batch is
ready. This is the always-on sensor-hub case, and it is the case where the FLPR
takes a peripheral interrupt. What wakes the core depends on the peripheral:

- SAADC: the SAADC samples on its own, paced by its internal timer, and fills a
  buffer by DMA. Its DONE interrupt wakes the core once per full buffer, and the
  interrupt handler copies the 160 samples out of the DMA buffer. After that the
  core only moves the batch through a queue to the loop; it never waits for a
  conversion.
- Accelerometers: the sensor's data-ready line on a GPIO. The driver's trigger
  thread reads each sample over the bus, because on both sensors reading the data
  is what clears data-ready. After 16 samples the loop wakes for the batch. The
  bus reads still cost core time, but nothing polls, and the sensor sets the pace.

The trigger is a build option for both images, not something sent over IPC. In
the offloaded build, the M33 image differs between triggers only in the name it
logs.

## Known limits

- Not run on a board. All fourteen combinations (three peripherals with two
  triggers, plus `compute_only`, each offloaded and baseline) build without
  compiler warnings, and the logic was reviewed against the driver sources.
  Nothing more has been verified.
- Peripheral handoff on the LM20. The M33 marks the peripheral `reserved` and
  the FLPR enables and drives it. Whether that is all the LM20 needs to give the
  FLPR the peripheral and its pins still has to be confirmed on hardware. The
  nRF54H20 does this by rerouting the interrupt in the app core devicetree, and
  that does not carry over because `cpuflpr_clic` is deleted from the LM20 M33
  devicetree. `compute_only` does not depend on this.
- The INT1 pins (P1.13, P1.14) have not been checked against the DK schematic.
- ADXL362: if a bus read in the data-ready path fails, the driver stalls. Its
  INT1 is edge-triggered, so data-ready stays high and no new edge arrives. The
  LIS2DH12 recovers, because its INT1 is level-triggered here and fires again
  when the driver unmasks it. Both behave the same on either core.
- GPIOTE is shared between the cores. In the offloaded build, the M33 must not
  take GPIO interrupts on the GPIOTE instance the FLPR uses. See
  [GPIOTE sharing](#gpiote-sharing).
- Only the nRF54LM20B is supported. Other boards stop the build with a message.

---

*Reference: what moves and what it costs, GPIOTE, in-tree precedent, measuring,
sizes, the data contract, memory, files and scope.*

## What moves to the FLPR

A workload does not move alone. It brings its driver, its buffers, its
devicetree and its result path. The transport, the build and the result path are
provided here, so what is left per peripheral is the driver and the data.

| Dependency | Moves to the FLPR? | What it takes |
| --- | --- | --- |
| Computation | Yes | Nothing. The same source is built for the FLPR. |
| Transport (ICMsg) | Provided | Nothing. |
| SAADC, TWIM and SPIM drivers (this sample) | Yes, same API | Devicetree wiring, done in `peripherals/<name>/`. |
| Sensor drivers (Zephyr sensor API) | Yes, same API | The driver's trigger thread and its 1 KB stack move too. |
| GPIO interrupts (sensor INT pins) | Yes | GPIOTE is shared hardware, see below. |
| Other nrfx drivers (UARTE, PWM, GPIO) | Yes, same API | Devicetree wiring, once per peripheral. |
| EasyDMA buffers | Yes | Must sit in RAM the FLPR and the peripheral can both reach. |
| BLE controller, MPSL | No | Arm-only libraries. They stay on the M33. |
| Crypto and security services | No | They stay on the M33. |

Moving a peripheral is wiring, with the same driver and the same NCS API. Once
per peripheral:

1. Give it to the FLPR: `peripheral.overlay` enables its node on the FLPR, and
   `hand_to_flpr.overlay` marks it `reserved` on the M33, so one core drives it.
2. Interrupt: usually nothing to do. Most interrupt lines are global to the SoC,
   so the FLPR gets the right vector. GRTC and GPIOTE are the exception (one line
   per domain) and pick their own line in the FLPR devicetree.
3. Ownership: the peripheral and its pins must belong to the FLPR domain. This is
   the item still to confirm on the LM20 (see [Known limits](#known-limits)).
4. Place its EasyDMA buffer where both the FLPR and the peripheral can reach it.

For the included peripherals, steps 1 and 2 are done in their folders. Their
DMA buffers are ordinary static variables, so they land in the RAM of the core
that runs the workload.

When a step genuinely has to stay on the M33, for example a result that must be
signed with the M33-only crypto, it costs one message: the FLPR does the heavy
part and hands the small remainder back. The M33 does not rejoin the per-batch
loop.

## GPIOTE sharing

The sensor INT pins use GPIOTE: gpiote20 for ports 1 and 3, gpiote30 for port 0.
Each GPIOTE has a separate interrupt group per core, so an event reaches the
right core. On the LM20B the FLPR uses group 0 and the secure M33 uses group 1
(`nrf54lm20b_interim.h`). The channels and the pin sense settings, however, are
one piece of hardware, and each core's nrfx driver allocates them without
knowing about the other core. So in the offloaded build the M33 must not take
GPIO interrupts on the same GPIOTE instance. This sample follows that rule: the
M33 takes no GPIO interrupts.

The non-secure M33 target (`cpuapp/ns`) uses group 0, the same as the FLPR. This
sample builds the secure target.

## In-tree precedent

Nordic already runs peripheral drivers on VPR cores in its tests. The `idle_adc`
benchmark reads the SAADC on the nRF54H20 PPR (a VPR core, the same family as
this FLPR) and checks the sampled value. TWIM, SPIM, UARTE and PWM have VPR tests
of the same kind, and the LM20 FLPR runs UARTE, QDEC and counter drivers in-tree.
Those tests build the FLPR as the main core; the split with the M33 used here is
what still needs a board run.

The SAADC interrupt is 213 in both the FLPR and the application MDK headers,
because interrupt numbers are global to the SoC. Enabling the SAADC on the FLPR
delivers its interrupt to the FLPR at that vector, with no override.

## Measuring

M33 active time needs no extra equipment. It comes from
`CONFIG_THREAD_RUNTIME_STATS` and `CONFIG_SCHED_THREAD_USAGE_ALL`: the console
prints the active share over each report interval, and idle is the rest.

The rate is results per second, counted on the M33 as results reach
`app_transmit()`, in both builds.

The acquire and process times come from the same loop on both cores
([common/workload_loop.c](common/workload_loop.c)) and are reported separately.
With the timer trigger, acquire is the peripheral read itself. With the
interrupt trigger, acquire only copies out a batch the peripheral already
collected. For the SAADC the collection is DMA, and the first copy, out of the
DMA buffer, happens in the interrupt, outside the acquire time; acquire then
copies the batch into the loop buffer. For the
accelerometers, the per-sample bus reads ran in the driver's trigger thread as
the samples arrived: they are in the M33 active figure of the baseline build,
not in the acquire time.

In the baseline build the workload runs in its own thread at the lowest
application priority, and the report runs above it, so even back to back
(`CONFIG_APP_SAMPLE_INTERVAL_MS=0`) the load line keeps printing.

For power, the loop drives a GPIO high while a batch is acquired and processed.
Put a Power Profiler Kit II or a scope on it and on the board current. The marker
is LED0 (P1.22) on the M33 in the baseline build, and LED1 (P1.25) on the FLPR in
the offloaded build.

Footprint comes from the build:

```
west build -t rom_report            # M33 image, ROM (RRAM)
west build -t ram_report            # M33 image, RAM
ninja -C build/remote rom_report    # FLPR image, ROM (RRAM)
ninja -C build/remote ram_report    # FLPR image, RAM
```

## Footprint

Measured from this template on v3.3.4, nRF54LM20B, as a reference point; your
workload will change it. Sizes in bytes.

| Peripheral, trigger | M33 offloaded (code / RAM) | FLPR image / RAM used of 96 KB | M33 baseline (code / RAM) |
| --- | --- | --- | --- |
| saadc, timer | 49000 / 16528 | 45212 / 56496 | 48808 / 11848 |
| saadc, interrupt | 48992 / 16528 | 43324 / 56532 | 47064 / 13652 |
| twim_lis2dh, timer | 49004 / 16528 | 46748 / 58048 | 49840 / 11816 |
| twim_lis2dh, interrupt | 49000 / 16528 | 51388 / 64304 | 53608 / 13504 |
| spim_adxl362, timer | 49004 / 16528 | 47580 / 58784 | 50988 / 11960 |
| spim_adxl362, interrupt | 49000 / 16528 | 49972 / 62784 | 52984 / 13656 |
| compute_only, timer | 49004 / 16528 | 39056 / 50224 | 43784 / 11576 |

The FLPR executes from RAM, so its RAM figure includes its code: code, data,
bss, stack and heap all sit in the 96 KB region. The offloaded M33 image is
within a few bytes of the same size for every peripheral, because it contains none of the workload. The
extra M33 RAM in the offloaded build is the inter-core transport, its heap and
the result queue. The baseline RAM includes the 2 KB stack of the workload
thread. The sensor interrupt builds use the most FLPR RAM because the driver's
trigger thread and its stack come along.

## The inter-core data contract

Defined once in [common/offload_protocol.h](common/offload_protocol.h) and shared
by both images. Four message types:

- `CONTROL` (M33 to FLPR): how many batches to run (0 means continuous) and the
  interval between them. Sent once at the start. The interrupt trigger ignores
  the interval, since the peripheral sets the rate.
- `RESULT` (FLPR to M33): one per batch that produced a result, with the batch
  index, the FLPR acquire and process times, and the opaque result and its
  length.
- `STATUS` (FLPR to M33): running, done.
- `ERROR` (FLPR to M33): a negative errno; the run stops, and the M33 logs it.

Both cores are little-endian, so the struct crosses as raw bytes with no
serialisation. If you extend it, keep fields aligned and rebuild both images
together (one `west build` always does).

## Memory placement

In the offloaded build the 511 KB of SRAM the board gives the M33 by default is
split like this:

| Region | Address | Size | Used for |
| --- | --- | --- | --- |
| `cpuapp_sram` | 0x20000000 | 411 KB | M33 code and data |
| `sram_rx` / `sram_tx` | 0x20066c00 | 2 KB + 2 KB | ICMsg shared buffers |
| `cpuflpr_sram_code_data` | 0x20067c00 | 96 KB | FLPR code, data, stack, heap |

The FLPR region is the top 96 KB, at the address the board devicetree gives
`cpuflpr_sram` by default. To move it, point
`cpuflpr_vpr`'s `execution-memory` at another region in
[boards/nrf54lm20dk_nrf54lm20b_cpuapp.overlay](boards/nrf54lm20dk_nrf54lm20b_cpuapp.overlay)
and shrink `cpuapp_sram` to match.

This matters more on the nRF54LM40, where the upper part of RAM takes two access
cycles against one for the lower part, so where the offloaded code and hot data
land affects what the offload is worth. The LM20 has no confirmed two-cycle
region, so the template uses the default placement.

## Files

| File | What it is |
| --- | --- |
| [app/workload_process.c](app/workload_process.c) | Your processing. Runs on whichever core runs the workload. |
| [app/app_transmit.c](app/app_transmit.c) | Your M33 code: what happens to each result. |
| [app/result.h](app/result.h) | The layout of one example result. |
| [peripherals/](peripherals/) | One folder per peripheral: devicetree, Kconfig and, where needed, `init` and `acquire`. |
| [peripherals/shared/sensor_accel.c](peripherals/shared/sensor_accel.c) | Shared accelerometer backend (Zephyr sensor API), both triggers. |
| [common/workload.h](common/workload.h) | The workload contract. |
| [common/workload_loop.c](common/workload_loop.c) | The batch loop and its timing, the same on both cores. |
| [common/workload.cmake](common/workload.cmake), [common/Kconfig.workload](common/Kconfig.workload) | Link the workload into whichever image runs it. |
| [common/offload_protocol.h](common/offload_protocol.h) | The inter-core message format. |
| [common/app_metrics.h](common/app_metrics.h) | Load, cycle and marker helpers. |
| [src/main.c](src/main.c) | M33: start-up and the load report. |
| [src/offloaded.c](src/offloaded.c) | M33, offloaded build: starts the FLPR and hands each result to `app_transmit()`. |
| [src/local.c](src/local.c) | M33, baseline build: runs the workload loop in a thread. |
| [remote/src/main.c](remote/src/main.c) | FLPR: the transport to the M33 around the same loop. |
| [boards/](boards/) | M33 overlays: FLPR launch and transport (offloaded), marker only (baseline). |
| [remote/boards/](remote/boards/) | FLPR overlay: its side of the transport. |
| [sysbuild.cmake](sysbuild.cmake), [Kconfig.sysbuild](Kconfig.sysbuild) | One build for both images, and the two build options. |

## Scope

- One FLPR. The nRF54LM20 in this SDK has a single FLPR. The nRF54LM40 adds a
  second VPR; the workload API, the data contract and the measurement carry over,
  and a second VPR is a second remote image in `sysbuild.cmake`.
- Security. This builds the secure M33 target on v3.3.4. The production part
  uses IronSide TE/TZ, which is not in this SDK. Nordic's current guidance is
  that non-secure software keeps configuring FLPR peripheral ownership, pins and
  DMA the same way, so the transport and the wiring here carry forward; that
  cannot be checked in v3.3.4. How the FLPR is started under IronSide is still to
  be confirmed. In this template the launch is confined to the devicetree nodes
  in the M33 board overlay.
- Numbers. The footprint table is measured from the build. Nothing at runtime has
  been measured yet.
