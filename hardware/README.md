# FPGA design (Zybo Z7-20, XC7Z020-1CLG400)

Programmable-logic side of the sonar: 16-channel PDM microphone capture into
DDR through AXI DMA, chirp generation and BRAM waveform playback to the
on-board SSM2603 codec.

## Layout

| Path | Content |
|---|---|
| `rtl/` | Verilog modules instantiated by the block design |
| `bd/pdm_dma_bd.tcl` | Block design (Zynq PS7, AXI DMA, GPIO, IIC, BRAM, ILA), exported with `write_bd_tcl` |
| `constraints/zybo_z7_sonar.xdc` | Pmod JA/JB microphone pins, codec pins, clock-domain crossing |
| `scripts/create_project.tcl` | Recreates the Vivado project in `hardware/vivado/` (git-ignored) |
| `scripts/build.tcl` | Synthesis, implementation, timing check and XSA export |
| `airborne_sonar.xsa` | Released hardware platform (bitstream included) used by the firmware |

## Build

Vivado 2025.2:

```bash
vivado -mode batch -source hardware/scripts/build.tcl
```

The script fails if implementation does not complete or setup timing is
violated. Output: `hardware/vivado/airborne_sonar.xsa`. To open the design in
the GUI instead, source `scripts/create_project.tcl` from the Tcl console.

## Data path

```
16 x PDM mic ──► top_pdm_axis_capture ──AXIS──► AXI DMA (S2MM) ──HP0──► DDR
   2.4 MHz        pdm16_to_axis_packer          IRQ_F2P[0] ──► GIC (ID 61)
                  2 instants (2 x 16 bit) per 32-bit word, 60,000 words = 50 ms

AXI GPIO chirp_freq/control ──► ssm2603_i2s_chirp_tx ──I2S──► SSM2603 ──► speaker
AXI BRAM ctrl ──► BRAM ──► pcm_bram_player ──┘ (source selected by axi_gpio_1)
AXI GPIO 0: shared trigger (starts capture and playback together), codec ready
```

Clocks: FCLK0 = 125 MHz (AXI and capture logic); `clock_gen_audio` derives
the 12.288 MHz codec master clock. The two domains are declared asynchronous
in the constraints.

## Address map

| Peripheral | Base | Size |
|---|---|---|
| `axi_bram_ctrl_0` (TX waveform) | `0x4000_0000` | 8 KiB |
| `axi_dma_0` (S2MM only, simple mode) | `0x4040_0000` | 64 KiB |
| `axi_gpio_0` (trigger, codec ready) | `0x4120_0000` | 64 KiB |
| `axi_gpio_chirp_control` | `0x4121_0000` | 64 KiB |
| `axi_gpio_chirp_freq` | `0x4122_0000` | 64 KiB |
| `axi_gpio_1` (TX source select) | `0x4123_0000` | 64 KiB |
| `axi_iic_0` (codec I2C) | `0x4160_0000` | 64 KiB |

The firmware checks these against `xparameters.h` at boot
(`sonar_profile_check`) and refuses to start on a mismatch.
