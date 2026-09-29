# Airborne Circular Sonar — Stage 2

Stage 2 provides an automated measurement cycle using FreeRTOS: trigger the speaker and microphones together, wait, move the motor by a user-defined number of steps, allow settling, and repeat until stopped. Microphone recordings are stored in board DDR memory and transferred to the laptop over Ethernet.

The application runs on the Zybo Z7-20’s ARM Cortex-A9 processor. The FPGA generates the chirp and acquires microphone data; the embedded software manages initialization, DMA, experiment sequencing, motor control, and data transfer.

## Hardware and software baseline

| Item | Configuration |
|---|---|
| Board | Digilent Zybo Z7-20 |
| Processor | `ps7_cortexa9_0` |
| Development tools | AMD Vivado and Vitis 2025.2 |
| Operating system | FreeRTOS |
| Platform | `Sonar_Airborn_platform` |
| Application | `Sonar_Airborn_app_component` |
| Hardware export | `Sonar_Airborn.xsa` |
| Matching bitstream | `Sonar_Airborn.bit`, included in the XSA |
| Processor initialization | Matching `ps7_init.tcl` from the same export |
| Speaker codec | SSM2603 |
| Motor controller | Pololu Tic 36v4, I²C address `0x0E` |
| Microphones | 16 PDM channels |
| Serial connection | 115200 baud |

The Stage 2 hardware reference is:

```text
D:\Tobezipped\SonarAirborn\AIrborneThesis\testing_microphone\project_1\Sonar_Airborn.xsa
```

Preserve this export with the Stage 2 release. A later XSA must be reviewed against the firmware’s register addresses, microphone packing, clock rate, capture length, and chirp duration before use.

Some source comments and console messages still mention `SonarParty.xsa`. These are older labels; they do not identify or verify the bitstream programmed into the board.

## Measurement cycle

After successful initialization, the application waits for a step count and a start command.

Each cycle follows this sequence:

| Event | Timing |
|---|---|
| Arm DMA and issue the shared speaker/microphone trigger | Start of cycle, `t = 0` |
| Speaker transmits one chirp | Nominally 0–25 ms |
| Microphones capture a fixed-length frame | Determined by the XSA |
| Wait before motor movement | Until 2 seconds after the trigger |
| Move the motor | User-defined signed step count |
| Wait after observed motor completion | 2 seconds |
| Repeat | Automatically, using the same step count |

The motor deadline is measured from the shared trigger, not from the end of recording. Movement duration depends on the step count, step resolution, speed, and acceleration.

These are scheduled timings. Motor commands and completion checks are subject to FreeRTOS scheduling and I²C latency.

### Current capture-length limitation

The supplied XSA specifies:

| Parameter | Value |
|---|---:|
| Chirp ROM length | 1,200 samples |
| Chirp playback repetitions | 1 |
| Nominal audio sample rate | 48 kHz |
| Nominal chirp duration | 25 ms |
| PDM clock | 2.4 MHz |
| Capture length | 3,750 × 32-bit words |
| Capture payload | 15,000 bytes |
| Nominal recording duration | 3.125 ms |

**The requested 50 ms microphone window is not yet implemented in this XSA.** The current recording therefore does not cover the full 25 ms chirp.

With the current packing and PDM clock, a 50 ms recording requires **60,000 words, or 240,000 bytes**. The FPGA export and software capture configuration must be updated together; increasing only the DMA request cannot extend the FPGA acquisition.

## Startup and commands

Startup runs the software self-tests, checks the BSP hardware declarations, initializes the peripherals, and starts the FreeRTOS tasks.

Wait for `READY` before starting an experiment. `READY` confirms initialization checks; it does not verify acoustic performance or physical motor movement.

Commands accept uppercase or lowercase letters.

| Command | Action |
|---|---|
| `N` | Enter a signed step count, then press Enter |
| `R` | Start repeating measurement cycles |
| `X` | Request stop |
| `S` | Show experiment state, step settings, receiver status, and DDR usage |
| `V` | Enter a step divisor, then press Enter |
| `?` | Show command help |

Example:

```text
N
20
Enter
R
```

The motor moves by the stored count after every acquisition cycle. Use a negative count to reverse the commanded direction. Accepted counts are −100,000 to +100,000, excluding zero.

The default divisor is **1**, meaning full steps. A divisor of 8 makes each entered step an eighth-step. The firmware applies and reads back the selected step mode. Physical direction depends on motor wiring.

`X` stops repetition and requests output shutdown. An in-flight microphone capture may finish draining before the application reaches `STOPPED`. Latched faults require a reset.

## Run from Vitis

The current application source is located at:

```text
D:\Vitis_Workspaces\Sonar_Airborn\Sonar_Airborn_app_component\src
```

For a new workspace:

1. Create a platform from the Stage 2 `Sonar_Airborn.xsa`.
2. Create a FreeRTOS domain for `ps7_cortexa9_0`.
3. Enable `lwip220` in **SOCKET_API** mode.
4. Create an application using that platform and domain.
5. Copy the maintained firmware files into the application’s `src` directory.
6. Retain the new application’s appropriate generated configuration and linker script.
7. Build the platform, then clean and build the application.
8. Run using the matching bitstream, processor initialization script, and application ELF.

The current BSP uses:

| Setting | Value |
|---|---:|
| FreeRTOS tick rate | 100 Hz |
| FreeRTOS heap | 524,288 bytes |
| Newlib reentrancy | Enabled |
| Preemption and mutexes | Enabled |
| lwIP API | Socket API |

The firmware also requires task-priority get/set support for Ethernet initialization.

## Control through the Python GUI

The desktop application is installed at:

```text
D:\GUIAPP
```

1. Run the firmware from Vitis.
2. Disconnect the Vitis serial monitor so the GUI can open the COM port.
3. Close any separate Ethernet receiver.
4. Launch `D:\GUIAPP\Start_Sonar_GUI.cmd`.
5. Select the board’s COM port and Ethernet adapter.
6. Choose a recordings folder; the default is `D:\SonarCaptures`.
7. Click **Connect**.
8. Enter the step count, select direction, and click **Start experiment**.
9. Click **Stop** to end repetition.

The GUI sends the step-setting and start commands in order, checking the board’s replies. It also receives and saves Ethernet recordings.

Keep the GUI connected until the board reports `STOPPED` and the queued DDR recordings have transferred.

**Only one application can own the serial port at a time.** Do not keep the GUI and Vitis serial monitor connected to the same COM port.

## Command-line Ethernet receiver

For manual operation, keep the serial monitor open and run this command separately in PowerShell:

```powershell
py D:\GUIAPP\host\receive_ethernet.py --output D:\SonarCaptures
```

Then enter `N`, the step count, Enter, and `R` on the serial terminal.

The receiver retries automatically after a connection interruption. It does not start or restart the experiment.

The firmware can start recording without Python while free DDR buffers remain. The GUI deliberately requires an Ethernet connection before enabling its Start button.

### Network configuration

| Setting | Value |
|---|---|
| Board address | `169.254.59.46` |
| Subnet mask | `255.255.0.0` |
| TCP capture port | `5001` |
| UDP discovery port | `5002` |
| Nominal batch interval | 30 seconds |

For the direct-cable setup, the laptop’s Ethernet adapter needs a different address in the same `/16` subnet.

Discovery finds the board; it does not change either device’s network configuration. The firmware has no client-address whitelist, but it does not automatically support every possible laptop subnet.

## Recordings and memory management

DMA writes captures into a fixed pool of **32 DDR buffers**. At the current frame length, the pool holds 480,000 bytes of capture payload, plus alignment and metadata.

Completed recordings are transferred in nominal 30-second batches. The receiver checks the header and payload CRCs, writes the binary data and metadata, and acknowledges the saved frame. A buffer becomes reusable only after a valid acknowledgement.

If the connection is interrupted:

- Unacknowledged recordings remain queued in DDR.
- Acquisition continues while free buffers remain.
- When the pool fills, the application pauses before starting another capture.
- Acquisition resumes automatically after acknowledged transfers free buffers.

Resetting or powering off the board discards recordings still held in DDR. Retransmissions can produce duplicate deliveries across reconnects.

Recordings are organized as:

```text
SonarCaptures/
└── session_<date_time>/
    └── connection_<number>/
        └── second_<board_capture_time>/
            ├── capture_<sequence>.bin
            └── capture_<sequence>.json
```

Each `.bin` contains raw packed PDM data. Its JSON sidecar includes sequence information, software timestamps, motor settings, nominal acquisition parameters, and checksums.

Folders use the capture’s board-relative time, not its Ethernet arrival time. Batch markers contain status information and do not create `.bin` recordings.

## Microphone data and timestamps

Eight microphone channels are sampled on one PDM clock edge and eight on the other. Each 32-bit word packs two successive 16-channel sample groups.

The transmitted timestamps record the processor’s trigger request and observation of DMA completion. **They are not FPGA-generated timestamps for individual samples.** Sample timing is inferred from the PDM clock and sample order.

CRC checks verify transfer integrity; they do not establish acoustic quality or prove that no samples were lost upstream of DMA.

### Plot a saved channel

Install the plotting dependencies:

```powershell
py -m pip install numpy matplotlib
```

Then use a real capture path:

```powershell
py D:\GUIAPP\host\plot_ethernet.py "D:\path\to\capture_0000000001.bin" --channel 0
```

The matching `.json` file must remain beside the `.bin`. Channels are numbered 0–15. The plot shows averaged PDM density as a diagnostic waveform, not a calibrated acoustic measurement.

## Software organization

| Responsibility | Main modules |
|---|---|
| Startup and initialization | `main.c`, `sonar_stage2.c` |
| Repeated measurement sequence | `sonar_cycle.c` |
| UART input and numeric parsing | `sonar_console.c`, `sonar_number.c` |
| Capture state and DMA integration | `sonar_mic.c`, `sonar_mic_zynq.c` |
| Speaker configuration | `sonar_speaker.c`, `sonar_speaker_board.c`, `sonar_codec_bus.c` |
| Motor control | `sonar_stepper.c`, `sonar_motor_zynq.c`, `sonar_ps_i2c.c` |
| DDR ownership and record format | `sonar_frames.c` |
| Ethernet transfer and discovery | `sonar_network.c` |
| Scheduling, health, and fault handling | `sonar_rtos.c`, `sonar_health.c`, `sonar_fault.c` |
| Timing, memory, and network settings | `sonar_stage2_config.h` |
| Motor communication and motion settings | `sonar_motor_config.h` |

Parameters are separated from component logic and hardware access to keep future changes localized.

## Tests and validation

Seven self-test suites are compiled into the firmware from files inside `src`. They run before normal operation and produce the serial `TEST PASS` messages. A separate `tests` directory is not required for these startup checks.

The suites cover configuration, health monitoring, microphone state handling, motor and speaker logic, driver behavior, and scan coordination. They use simulated inputs and hardware callbacks; they do not replace physical device tests.

Following the Stage 2 source cleanup:

- **158 startup self-tests passed, with 0 failures.**
- Additional host tests passed for repeated cycles, frame ownership, stepper control, and numeric input.
- All **35 retained C files** passed ARM syntax checks against the current BSP.

These checks did not include a new linked firmware build or a physical board run after cleanup. Rebuild in Vitis and verify capture, movement, stopping, and Ethernet recording after changes.

## Current scope

Stage 2 supports repeated acquisition, user-defined relative motor movement, bounded DDR buffering, Ethernet transfer, and desktop control.

The following remain outside this baseline:

- The requested 50 ms capture window.
- FPGA-generated acquisition timestamps and processor-visible transmit-completion/overflow status.
- Absolute-angle calibration and automatic homing.
- Standalone SD-card boot; firmware is currently launched through Vitis.
