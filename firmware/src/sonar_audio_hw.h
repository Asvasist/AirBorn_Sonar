#ifndef SONAR_AUDIO_HW_H
#define SONAR_AUDIO_HW_H
#include "xparameters.h"
#include <stdint.h>

/* Transmit-path peripherals from the BSP. A missing peripheral maps to zero
 * and makes initialization fail instead of writing to address 0. */
#ifdef XPAR_AXI_GPIO_CHIRP_FREQ_BASEADDR
#define SONAR_CHIRP_FREQ_BASEADDR XPAR_AXI_GPIO_CHIRP_FREQ_BASEADDR
#else
#define SONAR_CHIRP_FREQ_BASEADDR 0U
#endif
#ifdef XPAR_AXI_GPIO_CHIRP_CONTROL_BASEADDR
#define SONAR_CHIRP_CONTROL_BASEADDR XPAR_AXI_GPIO_CHIRP_CONTROL_BASEADDR
#else
#define SONAR_CHIRP_CONTROL_BASEADDR 0U
#endif
#ifdef XPAR_AXI_GPIO_1_BASEADDR
#define SONAR_TX_SOURCE_BASEADDR XPAR_AXI_GPIO_1_BASEADDR
#else
#define SONAR_TX_SOURCE_BASEADDR 0U
#endif
#ifdef XPAR_AXI_BRAM_CTRL_0_BASEADDR
#define SONAR_TX_BRAM_BASEADDR XPAR_AXI_BRAM_CTRL_0_BASEADDR
#define SONAR_TX_BRAM_BYTES (XPAR_AXI_BRAM_CTRL_0_HIGHADDR - XPAR_AXI_BRAM_CTRL_0_BASEADDR + 1U)
#else
#define SONAR_TX_BRAM_BASEADDR 0U
#define SONAR_TX_BRAM_BYTES 0U
#endif

/* Capability and status bits reported over the TCP 5003 protocol. */
#define AUDIO_CAP_GENERATE 1U
#define AUDIO_CAP_WAV      2U
#define AUDIO_CAP_WAV_GAIN 4U
#define AUDIO_CONFIG_VALID 1U
#define AUDIO_READY        2U
#define AUDIO_TX_BUSY      4U
#define AUDIO_TX_DONE      8U
#define AUDIO_ABORT_DONE  16U
#define AUDIO_CONFIG_ERROR 64U

#define SONAR_AUDIO_IO_TIMEOUT_MS 100U
#define SONAR_TX_TIMEOUT_MARGIN_US 10000U
#endif
