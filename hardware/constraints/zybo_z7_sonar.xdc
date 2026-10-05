## ============================================================
## Zybo Z7-20 - Airborne Sonar
##
## 16 independent SPH0641LU4H-1 PDM microphones
##
## SJ1 = GND:
##   DATA1 ... DATA8
##   Captured by FPGA on rising PDM clock tick
##
## SJ2 = VDD:
##   DATA11 ... DATA18
##   Captured by FPGA on falling PDM clock tick
##
## Common PDM clock = 2.4 MHz
## ============================================================
## AXI/FPGA domain and audio domain are intentionally asynchronous
set_clock_groups -asynchronous \
    -group [get_clocks clk_fpga_0] \
    -group [get_clocks clkout_mmcm]

## ============================================================
## ============================================================
## AUDIO CLOCK
##
## clkout_mmcm (~12.288 MHz) is generated from clk_fpga_0
## by clock_gen_audio_0. Vivado automatically derives this
## generated clock relationship.
## ============================================================
## ============================================================
## PDM CLOCK
## ============================================================
##
## Zybo JA1 = N15
##
## Connect this SAME clock to:
##   PCB CLKLIN1
##   PCB CLKLIN2
##
## Both PCB LMK1C1108 clock buffers therefore receive the
## same FPGA-generated 2.4 MHz clock.
## ============================================================

set_property PACKAGE_PIN N15 [get_ports {pdm_clk_out_0}]
set_property IOSTANDARD LVCMOS33 [get_ports {pdm_clk_out_0}]
set_property DRIVE 8 [get_ports {pdm_clk_out_0}]
set_property SLEW SLOW [get_ports {pdm_clk_out_0}]


## ============================================================
## SJ1 = GND
## MICROPHONES DATA1 ... DATA8
## ============================================================

## DATA1 -> JA2
set_property PACKAGE_PIN L14 [get_ports {pdm_data_sj1_0[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {pdm_data_sj1_0[0]}]

## DATA2 -> JA3
set_property PACKAGE_PIN K16 [get_ports {pdm_data_sj1_0[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {pdm_data_sj1_0[1]}]

## DATA3 -> JA4
set_property PACKAGE_PIN K14 [get_ports {pdm_data_sj1_0[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {pdm_data_sj1_0[2]}]

## DATA4 -> JA7
set_property PACKAGE_PIN N16 [get_ports {pdm_data_sj1_0[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {pdm_data_sj1_0[3]}]

## DATA5 -> JA8
set_property PACKAGE_PIN L15 [get_ports {pdm_data_sj1_0[4]}]
set_property IOSTANDARD LVCMOS33 [get_ports {pdm_data_sj1_0[4]}]

## DATA6 -> JA9
set_property PACKAGE_PIN J16 [get_ports {pdm_data_sj1_0[5]}]
set_property IOSTANDARD LVCMOS33 [get_ports {pdm_data_sj1_0[5]}]

## DATA7 -> JA10
set_property PACKAGE_PIN J14 [get_ports {pdm_data_sj1_0[6]}]
set_property IOSTANDARD LVCMOS33 [get_ports {pdm_data_sj1_0[6]}]

## DATA8 -> JC1
set_property PACKAGE_PIN V15 [get_ports {pdm_data_sj1_0[7]}]
set_property IOSTANDARD LVCMOS33 [get_ports {pdm_data_sj1_0[7]}]


## ============================================================
## SJ2 = VDD
## MICROPHONES DATA11 ... DATA18
##
## Zybo Z7-20 JB header
## ============================================================

## DATA11 -> JB1
set_property PACKAGE_PIN V8 [get_ports {pdm_data_sj2_0[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {pdm_data_sj2_0[0]}]

## DATA12 -> JB2
set_property PACKAGE_PIN W8 [get_ports {pdm_data_sj2_0[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {pdm_data_sj2_0[1]}]

## DATA13 -> JB3
set_property PACKAGE_PIN U7 [get_ports {pdm_data_sj2_0[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {pdm_data_sj2_0[2]}]

## DATA14 -> JB4
set_property PACKAGE_PIN V7 [get_ports {pdm_data_sj2_0[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {pdm_data_sj2_0[3]}]

## DATA15 -> JB7
set_property PACKAGE_PIN Y7 [get_ports {pdm_data_sj2_0[4]}]
set_property IOSTANDARD LVCMOS33 [get_ports {pdm_data_sj2_0[4]}]

## DATA16 -> JB8
set_property PACKAGE_PIN Y6 [get_ports {pdm_data_sj2_0[5]}]
set_property IOSTANDARD LVCMOS33 [get_ports {pdm_data_sj2_0[5]}]

## DATA17 -> JB9
set_property PACKAGE_PIN V6 [get_ports {pdm_data_sj2_0[6]}]
set_property IOSTANDARD LVCMOS33 [get_ports {pdm_data_sj2_0[6]}]

## DATA18 -> JB10
set_property PACKAGE_PIN W6 [get_ports {pdm_data_sj2_0[7]}]
set_property IOSTANDARD LVCMOS33 [get_ports {pdm_data_sj2_0[7]}]


## ============================================================
## SSM2603 AUDIO CODEC - SPEAKER/CHIRP
## Existing working connections - DO NOT CHANGE
## ============================================================

## Master Clock
set_property PACKAGE_PIN R17 [get_ports {ac_mclk}]
set_property IOSTANDARD LVCMOS33 [get_ports {ac_mclk}]

## Bit Clock
set_property PACKAGE_PIN R19 [get_ports {ac_bclk}]
set_property IOSTANDARD LVCMOS33 [get_ports {ac_bclk}]

## Playback Data
set_property PACKAGE_PIN R18 [get_ports {ac_pbdat}]
set_property IOSTANDARD LVCMOS33 [get_ports {ac_pbdat}]

## Playback Left/Right Clock
set_property PACKAGE_PIN T19 [get_ports {ac_pblrc}]
set_property IOSTANDARD LVCMOS33 [get_ports {ac_pblrc}]

## Codec Mute
set_property PACKAGE_PIN P18 [get_ports {ac_muten}]
set_property IOSTANDARD LVCMOS33 [get_ports {ac_muten}]

## I2C SCL
set_property PACKAGE_PIN N18 [get_ports {ac_iic_scl_io}]
set_property IOSTANDARD LVCMOS33 [get_ports {ac_iic_scl_io}]

## I2C SDA
set_property PACKAGE_PIN N17 [get_ports {ac_iic_sda_io}]
set_property IOSTANDARD LVCMOS33 [get_ports {ac_iic_sda_io}]


## ============================================================
## END
## ============================================================