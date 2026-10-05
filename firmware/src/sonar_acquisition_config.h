#ifndef SONAR_ACQUISITION_CONFIG_H
#define SONAR_ACQUISITION_CONFIG_H

/* Receiver is fixed at 50 ms. Runtime TX settings come from TCP 5003. */
#include "sonar_experiment_config.h"
#define SONAR_ACQ_CHANNELS 16U
#define SONAR_ACQ_WORDS 60000U
#define SONAR_ACQ_PDM_HZ 2400000U
#define SONAR_ACQ_RX_US ((SONAR_ACQ_WORDS * 2000000ULL) / SONAR_ACQ_PDM_HZ)
#define SONAR_ACQ_REQUESTED_RX_US 50000U
/* WAV can use mapped completion status; generated-chirp completion is estimated. */
#define SONAR_ACQ_MOTOR_START_US 2000000U /* Measured from the shared trigger. */
#define SONAR_ACQ_SETTLE_US 2000000U /* Measured from observed motor completion. */
#define SONAR_ACQ_CAPTURE_TIMEOUT_US 500000U
#define SONAR_ACQ_MOVE_TIMEOUT_US 60000000U
#define SONAR_ACQ_BATCH_MS 30000U
#define SONAR_ACQ_POOL_COUNT 32U
#define SONAR_ACQ_BYTES (SONAR_ACQ_WORDS * 4U)
#define SONAR_ACQ_SPAN ((SONAR_ACQ_BYTES + 31U) & ~31U)
#define SONAR_ACQ_MAX_STEPS 100000
#define SONAR_ACQ_DEFAULT_DIVISOR 1U
#define SONAR_MIC_RESET_TIMEOUT_MS 100U
#define SONAR_HARDWARE_NAME "hardware/airborne_sonar.xsa (TX <= 49 ms, RX 50 ms, 16 ch)"

/* Direct cable: any laptop address in 169.254.0.0/16 can reach this board.
 * No client IP whitelist. UDP discovery lets the receiver find this address.
 * This remains a fixed lab address, not DHCP or automatic address negotiation. */
#define SONAR_NET_IP "169.254.59.46"
#define SONAR_NET_MASK "255.255.0.0"
#define SONAR_NET_GATEWAY "0.0.0.0"
#define SONAR_NET_PORT 5001U
#define SONAR_NET_DISCOVERY_PORT 5002U
#define SONAR_NET_DEADLINE_MS 5000U
#define SONAR_NET_MAC {0x02U, 0x53U, 0x4fU, 0x4eU, 0x41U, 0x02U}

_Static_assert(SONAR_ACQ_CHANNELS == 16U, "Wire decoder requires 16-channel DDR packing");
_Static_assert(SONAR_ACQ_BYTES <= 0x03ffffffU, "Capture exceeds DMA length register");
_Static_assert(SONAR_ACQ_POOL_COUNT > 1U, "Capture and transfer need separate buffers");
/* Reserve enough slots for a full batch interval at the fastest current cycle,
 * plus one in flight. Revisit RAM capacity when updating hardware timings. */
_Static_assert(SONAR_ACQ_POOL_COUNT > 1ULL +
    (SONAR_ACQ_BATCH_MS * 1000ULL) /
    (SONAR_ACQ_MOTOR_START_US + SONAR_ACQ_SETTLE_US),
    "DDR pool is too small for the configured batch interval and cycle timing");
_Static_assert(SONAR_ACQ_MOTOR_START_US >= SONAR_CAPTURE_US + 10000U &&
    SONAR_ACQ_MOTOR_START_US >= SONAR_ACQ_RX_US,
    "Acoustic acquisition must finish before the motor deadline");
#endif
