# Airborne Circular Sonar — Stage 1

Stage 1 establishes the initial integrated software baseline for the Airborne Circular Sonar project. It provides FreeRTOS firmware for microphone capture, speaker control and motor operation, together with laptop tools for downloading and plotting captured data.

The application runs on the Zybo Z7-20’s ARM Cortex-A9 processor. The FPGA handles PDM acquisition and speaker waveform generation; the embedded software configures the peripherals, requests operations, monitors completion and manages captured data.

“Stage 1” identifies the first repository baseline. Some existing source comments and the startup banner still refer to earlier internal development stages, including “Stage 4.”

## Current functionality

- Configure and enable the SSM2603 speaker codec.
- Arm microphone DMA capture and issue the shared microphone/speaker trigger.
- Run the motor forward or backward through a Tic 36v4 controller.
- Download the completed microphone capture over UART.
- Verify the transfer using packet checksums, capture length and a complete-capture checksum.
- Plot PDM bit density, the demodulated waveform, spectrum and spectrogram.
- Run software self-tests at startup and monitor application health through FreeRTOS tasks.

The motor starts through a separate command. Starting it before a capture allows motion, transmission and recording to overlap; Stage 1 does not provide a single hardware trigger that starts all three devices simultaneously.

## Hardware and toolchain

| Item | Stage 1 configuration |
| --- | --- |
| Board | Digilent Zybo Z7-20 |
| FPGA device | xc7z020clg400-1 |
| Processor | ps7_cortexa9_0 |
| Development tools | AMD Vivado and Vitis 2025.2 |
| Vitis platform | airbornsonar_platform |
| Application | airborn_app |
| Operating system domain | freertos_ps7_cortexa9_0 |
| Hardware export | AirbornSonar_Initialtest.xsa |
| FPGA bitstream | AirbornSonar_Initialtest.bit |
| Processor initialization | Matching ps7_init.tcl |
| Serial connection | 115200 baud |

Preserve this hardware export alongside the Stage 1 release artifacts. A filename alone is insufficient to identify a hardware revision because later exports can reuse the same name.

When changing the Vivado design, export a new XSA, update the Vitis platform and regenerate its BSP. Review the software’s hardware assumptions and repeat the applicable tests before treating that combination as a new baseline.

## Project layout

```text
airborn_app/
├── src/                 Embedded firmware and application build files
├── host/                UART receiver and PDM plotting tools
├── tests/               Portable test runner and supporting verification files
├── vitis-comp.json      Vitis application component configuration
├── build/               Generated build output
├── _ide/                Local Vitis launch and IDE configuration
└── _backups/            Local development backups
```

Build output, backups and routine capture files are development artifacts rather than maintained application sources.

The Python tools belong in host. They execute on the laptop and are not compiled into the board application.

## Software organization

The firmware separates component behaviour from hardware access and task scheduling.

| Area | Main modules |
| --- | --- |
| Startup and software checks | main.c, sonar_selftest.c, component self-tests |
| FreeRTOS scheduling and health | sonar_rtos.c, sonar_health.c, sonar_fault.c |
| UART commands and shared console access | sonar_console.c |
| Microphone capture and DMA | sonar_mic.c, sonar_mic_task.c, sonar_mic_zynq.c |
| Speaker and codec control | sonar_speaker.c, sonar_speaker_board.c, sonar_codec_bus.c |
| Motor control | sonar_motor.c, sonar_motor_task.c, sonar_tic.c, sonar_motor_zynq.c |
| Shared FPGA GPIO access | sonar_pl_gpio.c |
| Capture export | sonar_export.c |
| Hardware compatibility checks | sonar_platform.c, sonar_profile.c |

The sonar_scan_* modules contain scan coordination logic, simulation and tests. The scan task is disabled in the current application entry point; automated physical scanning is not enabled in this baseline.

## Build and run in Vitis

1. Create or select a platform based on the Stage 1 XSA.
2. Select the FreeRTOS domain for ps7_cortexa9_0.
3. Create or open the airborn_app application.
4. Place the firmware files directly in its existing src directory.
5. Retain the required generated CMake configuration and the matching lscript.ld.
6. Clean and build the application.
7. Launch using the matching .bit file and processor initialization script.

The application CMake file uses the existing lscript.ld when no custom linker script is configured. It does not require the removed linker_files/lscript_a9.ld.in template.

Connect the serial monitor at 115200 baud. Startup runs the software self-tests before starting the scheduler. Check the self-test result and hardware initialization messages before issuing device commands.

## Serial commands

Commands are individual keys.

| Key | Action |
| --- | --- |
| c | Configure the speaker codec |
| u | Enable speaker output after codec configuration |
| m | Disable speaker output |
| f | Start the configured forward motor run |
| b | Start the configured reverse motor run |
| r | Start microphone capture and trigger speaker transmission |
| s | Report microphone, motor and codec status |
| d | Export the last completed capture |
| x | Request motor stop/de-energization, cancel speaker operation and cancel active capture/export |

The current motor configuration uses a nominal 10-second run with a configured speed of 3000 microsteps per second. Actual motion also depends on acceleration and the controller’s configuration.

User-entered step counts and target angles are not implemented in this baseline.

## Test the three devices

1. Press c and wait for CODEC READY.
2. Press u and confirm that the codec reports enabled=1.
3. Press f to start the motor.
4. While the motor is running, press r to trigger speaker transmission and microphone capture.
5. Wait for MIC READY bytes=15000.
6. Press s if a status report is needed.

The capture command requires the codec to be ready and enabled. Allow at least 2.1 seconds between capture triggers.

MIC READY confirms completion of the DMA capture. Downloading and examining the recorded signal is necessary to assess whether the microphone captured the expected sound.

## Download and plot a capture

Install the laptop dependencies once:

```powershell
py -m pip install pyserial numpy scipy matplotlib
```

After receiving MIC READY, disconnect the serial monitor from the COM port. Keep the board powered and the application running.

Open PowerShell and run:

```powershell
cd "D:\Vitis_Workspaces\AirbornSonar\airborn_app\host"
py .\receive_capture.py --port COM5 --output .\capture.bin --plot
```

Replace COM5 with the board’s actual serial port.

The receiver sends d automatically, verifies the transfer and creates:

- capture.bin: raw PDM capture data.
- capture.json: capture size, generation, clock rate, packing, software timestamps and checksum.

The --plot option opens the plotting tool after saving the capture. Omit it to download only. Use a new filename for another recording, or add --overwrite to deliberately replace existing output.

To plot a saved capture later, run from host:

```powershell
py .\pdm_plot.py .\capture.bin --pdm-hz 2400000 --bit-order msb --words 3750
```

Add --save .\capture.png to save the figure instead of opening a window.

The board retains the completed frame for another download until a new capture replaces it. Capture requests are blocked while an export is active.

## Capture format

| Property | Current value |
| --- | --- |
| PDM clock | 2.4 MHz |
| Capture length | 3750 words |
| Payload size | 15,000 bytes |
| Nominal recording duration | 50 ms |
| Storage | Little-endian 32-bit words |
| PDM sample order | Most significant bit first within each word |
| Default plot decimation | 50 |
| Resulting plot sample rate | 48 kHz |

The exported file contains only the capture payload. Protocol headers, diagnostic messages and buffer alignment padding are excluded.

The 50 ms recording covers only part of the approximately two-second speaker transmission sequence. A successful download confirms transfer integrity; it does not establish acoustic quality or verify FPGA overflow status.

## Testing

Component self-tests live alongside the firmware in src. The portable runner executes these checks on the laptop without accessing board peripherals.

From the application root, with a native GCC compiler available:

```powershell
.\tests\run_host_tests.ps1 -Compiler gcc
```

The suite covers configuration, health monitoring, microphone state handling, motor and speaker behaviour, driver logic and scan simulation. The previously validated suite contained 158 passing checks; rerun it after source changes.

The copied verification helpers have known limitations:

- check_target_compile.ps1 still expects an older restriction that prevents microphone hardware activation. That expectation no longer matches this application.
- verify_hardware.py expects an older hardware-reference directory and microphone-disabled configuration.
- The separate UART export/receiver tests are not currently included in this workspace’s tests directory.

Those older helpers require revision before they can serve as Stage 1 acceptance checks. The configuration under tests/target_config is a test fixture and must not replace the application’s generated BSP configuration.

Software tests complement hardware testing. They do not prove motor movement, speaker output or microphone signal quality.

## Changing parameters

- Microphone capture settings and timeouts: src/sonar_mic_config.h
- Motor speed, duration and watchdog timing: src/sonar_motor_config.h
- Application health timing: src/sonar_config.h
- Expected FPGA peripheral addresses and GPIO width: src/sonar_profile.h
- Capture transfer protocol: src/sonar_export.c and host/capture_protocol.py

Hardware-dependent changes must remain consistent with the selected XSA. In particular, changing the capture length or PDM clock in software alone does not change the FPGA implementation.

Stage 1 provides a baseline for component operation, shared speaker/capture triggering and recorded-data inspection. Automated angle-based scanning, configurable motor step commands and measured synchronization accuracy remain work for later stages.
