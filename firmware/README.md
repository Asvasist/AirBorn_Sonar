# Firmware (Zynq-7020 Cortex-A9, FreeRTOS)

Vitis 2025.2 application for core 0 of the Zybo Z7-20. It sequences the scan
(trigger, capture, motor move, settle), owns the DMA, codec and stepper
drivers, converts each 16-channel PDM capture to 96 kHz PCM, and serves four
Ethernet endpoints to the desktop application.

## Task model

| Task | Priority | Role |
|---|---|---|
| `supervisor` | 3 | Checks the experiment task's heartbeat (1 s timeout), restarts the CPU private watchdog (2 s) |
| `experiment` | 3 | Owns motor, speaker, DMA and the scan state machine (`sonar_experiment_coordinator.c`); wakes on keys and DMA completion, otherwise every tick |
| `tcpip` (lwIP) | 3 | lwIP core thread |
| `ethernet` | 2 | TCP 5001 capture records + UDP 5002 discovery; PDM-to-PCM conversion runs here (`sonar_ethernet.c`) |
| `eth_input` | 2 | EMAC receive thread |
| `console` | 2 | TCP 5004 console: log stream and key commands (`sonar_tcp_console.c`) |
| `sonar_control` | 2 | TCP 5003 configuration RPC (`sonar_control_server.c`); requests are executed by the experiment task |

Design rules:

- Capture buffers and DSP workspaces use static storage. FreeRTOS and lwIP
  use the BSP heap; network service tasks are created during initialization.
  The conversion loop does not allocate. The malloc-failed and
  stack-overflow hooks halt.
- Single core (CPU0). Shared state between tasks is either a queue or a short
  `taskENTER_CRITICAL` section; no critical section spans DMA or socket I/O.
- The DMA interrupt only records the completion and notifies its owner task.
  Cache maintenance and logging stay in task context.
- Hardware fails safe without the experiment task: playback is a finite
  one-shot, and the Tic controller stops the motor itself when keep-alive
  commands stop (1 s command timeout).
- A boot-time check (`sonar_profile_check`) compares the BSP's `xparameters.h`
  with the expected address map and halts on a mismatch.

## Data path

```
trigger (AXI GPIO) ─► FPGA captures 50 ms ─► AXI DMA ─► DDR frame pool (32 x 240 kB)
                                                            │ IRQ → experiment task
ethernet task, every 30 s:
  2.4 MHz PDM ─► 65-tap FIR / 5 ─► 480 kHz Q28
              ─► 577-tap FIR / 5 ─► 96 kHz PCM16 WAV ─► TCP 5001 ─► ACK3 ─► slot freed
```

Frames stay in DDR until the host acknowledges them; acquisition pauses
when all 32 slots are full. This supports retry after an Ethernet disconnect
while the board remains powered. A reset or power loss clears the volatile pool.

The receive filter's passband is 0–46 kHz, with a 46–50 kHz transition and a
stopband starting at 50 kHz before the final decimation. Both stages use
centered symmetric FIRs and compensate their group delay. Approximately
0.613 ms at each recording edge includes zero padding. Coefficients are in
`sonar_rx_fir_stages.h`; `sonar_rx_fir_reference.h` is used only by host tests.
The legacy coefficient headers are retained for the optional old-filter benchmark.

### Protocol compatibility

The capture connection uses `SONAR3` / `READY3` greetings, `ASN3` version-2
records with a 112-byte header and `ACK3` acknowledgements. Headers include
four 32-bit words each for the board boot and test identities, covered by
the header CRC. Configuration RPC uses `ASC2` version 2 and a 30-word reply,
including commands to establish the boot identity and prepare a new test.

The desktop receiver in `gui/` supports these protocol versions. It establishes
the boot identity, prepares a fresh test identity before each Start, and routes
captures through a durable mapping of boot/test identities. The existing
Start/Stop, upload, reconnect and test-folder features are retained.

## Source map

| Area | Files |
|---|---|
| Startup, supervision | `main.c`, `sonar_supervisor.c`, `sonar_health.c`, `sonar_config.c`, `sonar_fault.c` |
| Scan sequencing | `sonar_experiment_coordinator.c`, `sonar_measurement_cycle.c`, `sonar_scan360.c`, `sonar_integer_parser.c` |
| Capture | `sonar_mic.c` (state machine), `sonar_mic_zynq.c` (AXI DMA adapter), `sonar_frames.c` |
| Signal processing | `sonar_pdm_decimator.c`, `sonar_wav_writer.c`, `sonar_rx_processing.c` |
| Transmit and configuration | `sonar_tx_control.c`, `sonar_control_service.c`, `sonar_experiment_config.c`, `sonar_waveform_upload.c`, `sonar_pcm_playback_control.c`, `sonar_tx_bram.c`, `sonar_identity.c` |
| BSP properties and validation | `sonar_bsp_profile.c`, `sonar_bsp_validation.c` |
| Codec (SSM2603) | `sonar_codec_service.c` (state machine), `sonar_codec_board.c`, `sonar_codec_bus.c` (AXI IIC) |
| Motor (Pololu Tic) | `sonar_stepper.c`, `sonar_motor_zynq.c`, `sonar_ps_i2c.c` (PS I2C1) |
| Network | `sonar_ethernet.c`, `sonar_socket.c`, `sonar_tcp_console.c`, `sonar_console_buffer.c`, `sonar_control_*.c` |

Drivers are split into a hardware-independent state machine with an I/O
callback table and a thin Zynq adapter. The capture, codec and codec-bus state
machines are unit-tested on the host this way; the stepper driver is not yet.

## Build

### Platform (once)

In Vitis 2025.2, create a platform component from `hardware/airborne_sonar.xsa`:
operating system **freertos**, processor **ps7_cortexa9_0**, with boot
components (FSBL). The firmware needs these non-default BSP settings and checks
most of them at compile time:

| Setting | Value |
|---|---|
| Library `lwip220` | added, `api_mode = SOCKET_API` |
| `freertos_total_heap_size` | `524288` |
| `freertos_use_newlib_reent` | `true` |
| `freertos_use_idle_hook` | `true` |

Defaults that must stay: tick rate 100 Hz, preemption on, task notifications
on, stack-overflow check 2.

### Application

Create an **Empty Application** component on that platform, then replace its
`src/` with `firmware/src/` (keep the generated `Empty_applicationExample.cmake`
if Vitis regenerates it). `CMakeLists.txt` lists the sources explicitly and
builds with `-Wall -Wextra -Werror`.

`SONAR_RX_DSP_TIMING=ON` is the default and logs each 16-channel conversion.
`SONAR_RX_COMPARE_LEGACY=OFF` is the production default; enabling it builds
`sonar_pdm_decimator_legacy_benchmark.c` and processes the same raw frame with
both filters. It needs additional static memory and is intended for benchmarking.
The application build also produces a linker map and DSP stack-usage files.
`app.yaml`, the template CMake file and `vitis-comp.json` describe the current
Vitis component; regenerate platform paths and component metadata for a different
workspace. Generated build output and editor caches are excluded from Git.

### Run

Program the FPGA and run the ELF on `ps7_cortexa9_0` over JTAG from Vitis.
The board then prints its status on the UART (115200 baud) and on TCP 5004,
and the desktop application in `gui/` connects over Ethernet.

SD-card boot (`BOOT.BIN` from FSBL + bitstream + ELF with `bootgen`) has not
been validated yet.

## Tests

Hardware-independent modules are tested on the host:

```bash
cmake -S firmware/tests -B build/host-tests
cmake --build build/host-tests
ctest --test-dir build/host-tests --output-on-failure
```

93 tests cover the capture, codec and supervision state machines, the scan
sequencer, frame pool, wire protocols, console buffer and a bit-exact check of
the optimized two-stage PDM decimator against its direct-form reference. The
host build sets `SONAR_PDM_TEST_REFERENCE=1`; the target build does not include
that reference array. The updated wire-header tests check boot/test identity
fields and their CRC, and the identity test rejects a different boot and a
repeated Start identity. CI also runs
them under AddressSanitizer and UBSan, and gates on clang-format and cppcheck.
