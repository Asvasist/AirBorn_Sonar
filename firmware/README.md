# Firmware (Zynq-7020 Cortex-A9, FreeRTOS)

Vitis 2025.2 application for core 0 of the Zybo Z7-20. It sequences the scan
(trigger, capture, motor move, settle), owns the DMA, codec and stepper
drivers, converts each 16-channel PDM capture to 96 kHz PCM, and serves four
Ethernet endpoints to the desktop application.

## Task model

| Task | Priority | Role |
|---|---|---|
| `supervisor` | 3 | Checks the experiment task's heartbeat (1 s timeout), restarts the CPU private watchdog (2 s) |
| `experiment` | 3 | Owns motor, speaker, DMA and the scan state machine (`sonar_sequencer.c`); wakes on keys and DMA completion, otherwise every tick |
| `tcpip` (lwIP) | 3 | lwIP core thread |
| `ethernet` | 2 | TCP 5001 capture records + UDP 5002 discovery; PDM-to-PCM conversion runs here (`sonar_network.c`) |
| `eth_input` | 2 | EMAC receive thread |
| `console` | 2 | TCP 5004 console: log stream and key commands (`sonar_tcp_console.c`) |
| `sonar_control` | 2 | TCP 5003 configuration RPC (`sonar_control_server.c`); requests are executed by the experiment task |

Design rules:

- All RTOS objects are created once before the scheduler starts; nothing
  allocates afterwards. The malloc-failed and stack-overflow hooks halt.
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
ethernet task, every 30 s:  PDM ─► 801-tap FIR / 25 ─► PCM16 WAV ─► TCP 5001 ─► ACK ─► slot freed
```

Frames stay in DDR until the host acknowledges them, so a dropped connection
loses nothing; acquisition pauses when all 32 slots are full.

## Source map

| Area | Files |
|---|---|
| Startup, supervision | `main.c`, `sonar_rtos.c`, `sonar_health.c`, `sonar_config.c`, `sonar_fault.c` |
| Scan sequencing | `sonar_sequencer.c`, `sonar_cycle.c`, `sonar_scan360.c`, `sonar_number.c` |
| Capture | `sonar_mic.c` (state machine), `sonar_mic_zynq.c` (AXI DMA adapter), `sonar_frames.c` |
| Signal processing | `sonar_pdm.c`, `sonar_wav.c`, `sonar_processing.c` |
| Transmit | `sonar_chirp.c`, `sonar_experiment*.c`, `sonar_waveform.c`, `sonar_wav_player.c`, `sonar_tx_bram.c` |
| Codec (SSM2603) | `sonar_speaker.c` (state machine), `sonar_speaker_board.c`, `sonar_codec_bus.c` (AXI IIC) |
| Motor (Pololu Tic) | `sonar_stepper.c`, `sonar_motor_zynq.c`, `sonar_ps_i2c.c` (PS I2C1) |
| Network | `sonar_network.c`, `sonar_socket.c`, `sonar_tcp_console.c`, `sonar_console_buffer.c`, `sonar_control_*.c` |

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

92 tests cover the capture, codec and supervision state machines, the scan
sequencer, frame pool, wire protocols, console buffer and a bit-exact check of
the optimized PDM decimator against its direct-form reference. CI also runs
them under AddressSanitizer and UBSan, and gates on clang-format and cppcheck.
