#ifndef SONAR_AUDIO_HW_H
#define SONAR_AUDIO_HW_H
#include "xparameters.h"
#include <stdint.h>
/* Existing generated-chirp GPIOs. Missing BSP macros map to zero and prevent
 * initialization. Preserve any verified compile-definition overrides.
 * WAV mapping is now exclusively in sonar_wav_board_config.h.
 * Legacy AUDIO_* register offsets below remain for source compatibility;
 * the new WAV driver does NOT access that proposed control/status block. */
#ifndef SONAR_CHIRP_FREQ_BASEADDR
#ifdef XPAR_AXI_GPIO_CHIRP_FREQ_BASEADDR
#define SONAR_CHIRP_FREQ_BASEADDR XPAR_AXI_GPIO_CHIRP_FREQ_BASEADDR
#else
#define SONAR_CHIRP_FREQ_BASEADDR 0U
#endif
#endif
#ifndef SONAR_CHIRP_CONTROL_BASEADDR
#ifdef XPAR_AXI_GPIO_CHIRP_CONTROL_BASEADDR
#define SONAR_CHIRP_CONTROL_BASEADDR XPAR_AXI_GPIO_CHIRP_CONTROL_BASEADDR
#else
#define SONAR_CHIRP_CONTROL_BASEADDR 0U
#endif
#endif
#ifndef SONAR_AUDIO_CONTROL_BASEADDR
#ifdef XPAR_SONAR_AUDIO_CONTROL_0_BASEADDR
#define SONAR_AUDIO_CONTROL_BASEADDR XPAR_SONAR_AUDIO_CONTROL_0_BASEADDR
#else
#define SONAR_AUDIO_CONTROL_BASEADDR 0U
#endif
#endif
#ifndef SONAR_TX_BRAM_BASEADDR
#ifdef XPAR_AXI_BRAM_CTRL_0_BASEADDR
#define SONAR_TX_BRAM_BASEADDR XPAR_AXI_BRAM_CTRL_0_BASEADDR
#else
#define SONAR_TX_BRAM_BASEADDR 0U
#endif
#endif
#ifndef SONAR_TX_BRAM_HIGHADDR
#ifdef XPAR_AXI_BRAM_CTRL_0_HIGHADDR
#define SONAR_TX_BRAM_HIGHADDR XPAR_AXI_BRAM_CTRL_0_HIGHADDR
#else
#define SONAR_TX_BRAM_HIGHADDR 0U
#endif
#endif
#define SONAR_TX_BRAM_BANK_BYTES 8192U
#define SONAR_TX_BRAM_TOTAL_BYTES (2U * SONAR_TX_BRAM_BANK_BYTES)

#define SONAR_AUDIO_ID 0x534e5231U
#define SONAR_AUDIO_ABI 1U
#define AUDIO_ID          0x00U
#define AUDIO_ABI         0x04U
#define AUDIO_CAPS        0x08U
#define AUDIO_COMMAND     0x0cU
#define AUDIO_STATUS      0x10U
#define AUDIO_MODE        0x14U
#define AUDIO_CONFIG_ID   0x18U
#define AUDIO_ACK_ID      0x1cU
#define AUDIO_START_MAX_US 0x20U
#define AUDIO_RX_WORDS    0x24U
#define AUDIO_SAMPLE_HZ   0x28U
#define AUDIO_PDM_HZ      0x2cU
#define AUDIO_WAVE_OFFSET 0x30U
#define AUDIO_CAP_GENERATE 1U
#define AUDIO_CAP_WAV      2U
#define AUDIO_CAP_WAV_GAIN 4U
#define AUDIO_CAP_WAV_DONE 8U
#define AUDIO_APPLY        1U
#define AUDIO_PREPARE      2U
#define AUDIO_ABORT        4U
#define AUDIO_CONFIG_VALID 1U
#define AUDIO_READY        2U
#define AUDIO_TX_BUSY      4U
#define AUDIO_TX_DONE      8U
#define AUDIO_ABORT_DONE  16U
#define AUDIO_UNDERRUN    32U
#define AUDIO_CONFIG_ERROR 64U
#define AUDIO_RX_OVERFLOW 128U
#define AUDIO_ERRORS (AUDIO_UNDERRUN | AUDIO_CONFIG_ERROR | AUDIO_RX_OVERFLOW)
#define SONAR_AUDIO_IO_TIMEOUT_MS 100U
#define SONAR_TX_TIMEOUT_MARGIN_US 10000U
#endif
