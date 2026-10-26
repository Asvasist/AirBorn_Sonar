#ifndef SONAR_TX_STATUS_CONFIG_H
#define SONAR_TX_STATUS_CONFIG_H
#include "xparameters.h"

/* REQUIRED hardware interface: AXI GPIO channel 1, all inputs.
 * Name the Vivado instance axi_gpio_tx_status and regenerate the BSP.
 * An explicit verified base/width may alternatively be supplied by the build.
 * Zero means absent: never guess a numbered GPIO or read address zero. */
#ifndef SONAR_TX_STATUS_BASEADDR
#ifdef XPAR_AXI_GPIO_TX_STATUS_BASEADDR
#define SONAR_TX_STATUS_BASEADDR XPAR_AXI_GPIO_TX_STATUS_BASEADDR
#else
#define SONAR_TX_STATUS_BASEADDR 0U
#endif
#endif
#ifndef SONAR_TX_STATUS_GPIO_WIDTH
#ifdef XPAR_AXI_GPIO_TX_STATUS_GPIO_WIDTH
#define SONAR_TX_STATUS_GPIO_WIDTH XPAR_AXI_GPIO_TX_STATUS_GPIO_WIDTH
#else
#define SONAR_TX_STATUS_GPIO_WIDTH 0U
#endif
#endif

/* Supplied Project_Thesis.zip: 4096 words / 16 KiB.
 * Actual physical BRAM depth, not merely its processor address aperture.
 * Change only after resizing the memory in Vivado and exporting matching HW. */
#ifndef SONAR_TX_BRAM_DEPTH_WORDS
#define SONAR_TX_BRAM_DEPTH_WORDS 4096U
#endif

#define TX_DONE_MASK   (1U << 0) /* sticky; cleared on accepted audio start */
#define TX_ACTIVE_MASK (1U << 1) /* audio request armed OR transmitter busy */
#define TX_TOGGLE_MASK (1U << 2) /* flips once after the final I2S frame */
#endif
