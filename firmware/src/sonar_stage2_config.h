#ifndef SONAR_STAGE2_CONFIG_H
#define SONAR_STAGE2_CONFIG_H

/* Receiver is fixed at 50 ms. Runtime TX settings come from TCP 5003. */
#include "sonar_experiment_config.h"
#define SONAR_STAGE2_CHANNELS 16U
#define SONAR_STAGE2_WORDS 60000U
#define SONAR_STAGE2_PDM_HZ 2400000U
#define SONAR_STAGE2_RX_US ((SONAR_STAGE2_WORDS * 2000000ULL) / SONAR_STAGE2_PDM_HZ)
#define SONAR_STAGE2_REQUESTED_RX_US 50000U
/* WAV can use mapped completion status; generated-chirp completion is estimated. */
#define SONAR_STAGE2_MOTOR_START_US 2000000U /* Measured from the shared trigger. */
#define SONAR_STAGE2_SETTLE_US 2000000U /* Measured from observed motor completion. */
#define SONAR_STAGE2_CAPTURE_TIMEOUT_US 500000U
#define SONAR_STAGE2_MOVE_TIMEOUT_US 60000000U
#define SONAR_STAGE2_BATCH_MS 30000U
#define SONAR_STAGE2_POOL_COUNT 32U
#define SONAR_STAGE2_BYTES (SONAR_STAGE2_WORDS * 4U)
#define SONAR_STAGE2_SPAN ((SONAR_STAGE2_BYTES + 31U) & ~31U)
#define SONAR_STAGE2_MAX_STEPS 100000
#define SONAR_STAGE2_DEFAULT_DIVISOR 1U

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

_Static_assert(SONAR_STAGE2_CHANNELS == 16U, "Wire decoder requires 16-channel DDR packing");
_Static_assert(SONAR_STAGE2_BYTES <= 0x03ffffffU, "Capture exceeds DMA length register");
_Static_assert(SONAR_STAGE2_POOL_COUNT > 1U, "Capture and transfer need separate buffers");
/* Reserve enough slots for a full batch interval at the fastest current cycle,
 * plus one in flight. Revisit RAM capacity when updating hardware timings. */
_Static_assert(SONAR_STAGE2_POOL_COUNT > 1ULL +
    (SONAR_STAGE2_BATCH_MS * 1000ULL) /
    (SONAR_STAGE2_MOTOR_START_US + SONAR_STAGE2_SETTLE_US),
    "DDR pool is too small for the configured batch interval and cycle timing");
_Static_assert(SONAR_STAGE2_MOTOR_START_US >= SONAR_CAPTURE_US + 10000U &&
    SONAR_STAGE2_MOTOR_START_US >= SONAR_STAGE2_RX_US,
    "Acoustic acquisition must finish before the motor deadline");
#endif
