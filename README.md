# Thesis Airborn Sonar

This repository contains the FreeRTOS-based embedded firmware and hardware export files for the Zynq-7020 airborne sonar thesis project.

## Current target

The final firmware will run as a FreeRTOS application on the ARM Cortex-A9 Processing System of the Zynq-7020. The Programmable Logic will handle timing-critical sonar capture functions.

## Final deployment goal

The final deliverable should include a reproducible boot image:

```text
BOOT.BIN = FSBL + FPGA bitstream + FreeRTOS application
