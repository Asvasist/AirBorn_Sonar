#ifndef SONAR_AUDIO_HW_H
#define SONAR_AUDIO_HW_H
#include "xparameters.h"
#include "sonar_tx_status_config.h"
#include "sonar_experiment_config.h"
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
#define SONAR_TX_BRAM_BYTES    (XPAR_AXI_BRAM_CTRL_0_HIGHADDR - XPAR_AXI_BRAM_CTRL_0_BASEADDR + 1U)
#else
#define SONAR_TX_BRAM_BASEADDR 0U
#define SONAR_TX_BRAM_BYTES    0U
#endif

/* Capability and status bits reported over the TCP 5003 protocol. */
#define AUDIO_CAP_GENERATE      1U
#define AUDIO_CAP_WAV           2U
#define AUDIO_CAP_WAV_GAIN      4U
#define AUDIO_CAP_HW_COMPLETION 8U
#define AUDIO_CONFIG_VALID      1U
#define AUDIO_READY             2U
#define AUDIO_TX_BUSY           4U
#define AUDIO_TX_DONE           8U
#define AUDIO_ABORT_DONE        16U
#define AUDIO_CONFIG_ERROR      64U

/* Reject a stale BSP at build time; never silently use timed completion or
 * a smaller aperture. These declarations cannot identify the loaded bitstream. */
_Static_assert(SONAR_TX_STATUS_BASEADDR != 0U && SONAR_TX_STATUS_GPIO_WIDTH == 3U,
               "Update platform XSA/regenerate BSP: axi_gpio_tx_status inputs width 3 required");
_Static_assert(SONAR_TX_BRAM_BASEADDR != 0U && SONAR_TX_BRAM_DEPTH_WORDS == 4096U &&
                   SONAR_TX_BRAM_BYTES == 16384U,
               "Update platform XSA/regenerate BSP: 4096-word / 16-KiB playback BRAM required");
_Static_assert(SONAR_TX_MAX_SAMPLES == 4704U && SONAR_AUDIO_HZ == 96000U,
               "This patch requires the fixed-96-kHz, 49-ms playback configuration");

#define SONAR_AUDIO_IO_TIMEOUT_MS  100U
#define SONAR_TX_TIMEOUT_MARGIN_US 10000U
#endif
