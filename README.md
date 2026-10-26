# Airborne Circular Sonar

[![CI](https://github.com/Asvasist/AirBorn_Sonar/actions/workflows/ci.yml/badge.svg)](https://github.com/Asvasist/AirBorn_Sonar/actions/workflows/ci.yml)

An in-air acoustic scanner on a Digilent Zybo Z7-20 (Xilinx Zynq-7020). A
speaker emits a chirp, a 16-microphone PDM array records the echoes, and a
stepper motor rotates the array plate through one full revolution in a
user-chosen number of positions. Captures are buffered in DDR and streamed to a
desktop application over Ethernet.

```mermaid
flowchart LR
    subgraph PL[Zynq PL - FPGA]
        CAP[16-ch PDM capture<br/>2.4 MHz] --> DMA[AXI DMA S2MM]
        TX[Chirp generator /<br/>BRAM player] --> I2S[I2S to SSM2603]
    end
    subgraph PS[Zynq PS - Cortex-A9, FreeRTOS]
        SEQ[Scan sequencer] --> DMA
        SEQ --> TX
        SEQ --> TIC[Tic stepper<br/>PS I2C]
        DMA --> DDR[(DDR frame pool)]
        DDR --> DSP[PDM to 96 kHz PCM]
        DSP --> NET[lwIP sockets]
    end
    NET <-->|TCP 5001/5003/5004<br/>UDP 5002| GUI[Desktop app<br/>Python / Tk]
    MICS((16 PDM mics)) --> CAP
    I2S --> SPK((Speaker))
    TIC --> MOT((Stepper + plate))
```

## Measurement cycle

For each of the N positions per revolution (1–1000, chosen by the user):

1. Trigger playback and capture together (one FPGA GPIO edge).
2. Transmit a chirp (generated LFM sweep up to 49 ms, or an uploaded WAV of up to
   4096 samples = 42.67 ms, at 96 kHz)
   while all 16 microphones record 50 ms.
3. 2 s after the trigger, move the plate by 1/N of a revolution
   (88,000 Tic position units per 360° at 400 microsteps/s; moves are rounded so
   they sum exactly).
4. Wait 2 s after the motor reports completion, then repeat.

## Repository

| Directory | Content |
|---|---|
| [`hardware/`](hardware/README.md) | Vivado block design (Tcl), RTL, constraints, reproducible build, released XSA |
| [`firmware/`](firmware/README.md) | FreeRTOS application, host unit tests |
| [`gui/`](gui/README.md) | Desktop application for configuration, scan control and recording |

## Status

The firmware in this repository is the thesis-final version. It requires the
transmit-status hardware interface described in the
[firmware README](firmware/README.md#transmit-completion-and-hardware-requirements);
the `hardware/` sources and XSA must match it before the firmware will build.

## Quick start

1. Program the board: build the platform and application in Vitis 2025.2 from
   `hardware/airborne_sonar.xsa` and `firmware/` (details in
   [firmware/README.md](firmware/README.md)), then run on `ps7_cortexa9_0`.
2. Connect the board's Ethernet port directly to the PC.
3. Start `gui\Start_Sonar_GUI.cmd`, connect, apply a transmit setting, choose
   the number of positions and press **Start**. The GUI supports the current
   `ASN3` capture and `ASC2` version-2 control protocol and prepares a fresh
   recording identity for every test.

## Engineering

- **Reproducible hardware:** `vivado -mode batch -source hardware/scripts/build.tcl`
  rebuilds the bitstream from sources, checks timing and exports the XSA.
- **Supervision:** a heartbeat monitor on the task that owns the actuators,
  backed by the Cortex-A9 private watchdog; outputs are safe even if that task
  stalls.
- **Loss-free transfer:** captures stay in a DDR slot until the
  host acknowledges the CRC-checked record.
- **Tests:** 93 host unit tests for the firmware (run under ASan/UBSan in CI)
  and 86 for the GUI; clang-format and cppcheck gates.

## License

GPL-3.0, see [LICENSE](LICENSE).
