#ifndef SONAR_WAV_BOARD_CONFIG_H
#define SONAR_WAV_BOARD_CONFIG_H

/* Fixed-96-kHz WAV adapter. See HARDWARE_MAPPING.txt.
 * The user will supply the matching Vivado source-select connection.
 * Existing BRAM is 8 KiB. Each word holds original PCM16 in bits 31:16 and
 * playback PCM16 in bits 15:0; the FPGA reads ONLY the lower 16 bits.
 * Vitis applies linear amplitude in BRAM, so no FPGA gain register is needed.
 * A missing mode GPIO BSP macro prevents access to that missing peripheral.
 */
#include "xparameters.h"
#include "sonar_audio_hw.h"
#include <stdint.h>

#ifndef SONAR_WAV_ENABLED
#define SONAR_WAV_ENABLED 1
#endif
/* Use actual implemented BRAM bytes, not a larger AXI address aperture. */
#ifndef SONAR_WAV_BRAM_BASE
#if defined(XPAR_AXI_BRAM_CTRL_0_BASEADDR)
#define SONAR_WAV_BRAM_BASE XPAR_AXI_BRAM_CTRL_0_BASEADDR
#elif defined(XPAR_AXI_BRAM_CTRL_0_S_AXI_BASEADDR)
#define SONAR_WAV_BRAM_BASE XPAR_AXI_BRAM_CTRL_0_S_AXI_BASEADDR
#else
#define SONAR_WAV_BRAM_BASE 0U
#endif
#endif
#ifndef SONAR_WAV_BRAM_BYTES
#define SONAR_WAV_BRAM_BYTES 8192U
#endif
/* 2: sample0 in bits 15:0, sample1 in bits 31:16 of each AXI word.
 * 4: original PCM16 in the upper 16 bits; scaled PCM16 in the lower 16 bits. */
#ifndef SONAR_WAV_SAMPLE_STRIDE
#define SONAR_WAV_SAMPLE_STRIDE 4U
#endif

/* Dedicated full 32-bit registers, owned exclusively by this adapter.
 * Do NOT point MODE_REG at the shared trigger/codec-ready GPIO.
 * These are absolute DATA register addresses, not necessarily IP base addresses.
 * Optional TRI addresses are AXI GPIO direction registers (0 means absent).
 */
#ifndef SONAR_WAV_MODE_REG
#define SONAR_WAV_MODE_REG XPAR_AXI_GPIO_1_BASEADDR
#endif
#ifndef SONAR_WAV_GENERATE_VALUE
#define SONAR_WAV_GENERATE_VALUE 0U
#endif
#ifndef SONAR_WAV_PLAYBACK_VALUE
#define SONAR_WAV_PLAYBACK_VALUE 1U
#endif
#ifndef SONAR_WAV_MODE_TRI
#define SONAR_WAV_MODE_TRI (SONAR_WAV_MODE_REG?SONAR_WAV_MODE_REG+4U:0U)
#endif
#ifndef SONAR_WAV_COUNT_REG
#define SONAR_WAV_COUNT_REG SONAR_CHIRP_CONTROL_BASEADDR
#endif
#ifndef SONAR_WAV_COUNT_TRI
#define SONAR_WAV_COUNT_TRI (SONAR_CHIRP_CONTROL_BASEADDR+4U)
#endif
/* 0 writes N; 1 writes N-1. Choose what the reader actually expects. */
#ifndef SONAR_WAV_COUNT_MINUS_ONE
#define SONAR_WAV_COUNT_MINUS_ONE 0U
#endif
/* Optional linear gain register. With GAIN_REG=0 and stride=4, Vitis scales playback PCM in place.
 * Stride=2 without a hardware gain register supports only 100%.
 * FULL_SCALE is the exact register value for unity gain, e.g. 32767 or 32768.
 * It is NOT an SSM2603 logarithmic volume register. */
#ifndef SONAR_WAV_GAIN_REG
#define SONAR_WAV_GAIN_REG 0U
#endif
#ifndef SONAR_WAV_GAIN_TRI
#define SONAR_WAV_GAIN_TRI 0U
#endif
#ifndef SONAR_WAV_GAIN_FULL_SCALE
#define SONAR_WAV_GAIN_FULL_SCALE 32767U
#endif

/* Optional dedicated command register. REWIND must reset the read index and
 * clear sticky completion/errors WITHOUT starting sound. A command is held for
 * one RTOS tick, then IDLE is written. Values of zero for REWIND/ABORT mean absent.
 * If REWIND is absent, hardware itself must rewind on each shared trigger.
 * If ABORT is absent, Stop mutes output and drains the finite playback.
 */
#ifndef SONAR_WAV_COMMAND_REG
#define SONAR_WAV_COMMAND_REG 0U
#endif
#ifndef SONAR_WAV_COMMAND_TRI
#define SONAR_WAV_COMMAND_TRI 0U
#endif
#ifndef SONAR_WAV_COMMAND_IDLE
#define SONAR_WAV_COMMAND_IDLE 0U
#endif
#ifndef SONAR_WAV_REWIND_VALUE
#define SONAR_WAV_REWIND_VALUE 0U
#endif
#ifndef SONAR_WAV_ABORT_VALUE
#define SONAR_WAV_ABORT_VALUE 0U
#endif

/* Optional read-only, active-high, PS-synchronized status bits.
 * DONE must latch the last sample being PLAYED, not BRAM upload completion.
 * Clear-on-read status is unsupported. With DONE_MASK=0, completion is an
 * explicitly reported time estimate; it cannot detect missing sound/underrun.
 */
#ifndef SONAR_WAV_STATUS_REG
#define SONAR_WAV_STATUS_REG 0U
#endif
#ifndef SONAR_WAV_READY_MASK
#define SONAR_WAV_READY_MASK 0U
#endif
#ifndef SONAR_WAV_BUSY_MASK
#define SONAR_WAV_BUSY_MASK 0U
#endif
#ifndef SONAR_WAV_DONE_MASK
#define SONAR_WAV_DONE_MASK 0U
#endif
#ifndef SONAR_WAV_ERROR_MASK
#define SONAR_WAV_ERROR_MASK 0U
#endif

#endif
