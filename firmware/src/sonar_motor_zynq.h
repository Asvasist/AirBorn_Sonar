#ifndef SONAR_MOTOR_ZYNQ_H
#define SONAR_MOTOR_ZYNQ_H
#include "sonar_tic.h"
typedef struct {
    const char *init_reason;
    uint32_t aper, reset, mio12, mio13, tri, loopback;
    uint32_t bus_status, failed_irq, failed_length;
    uint8_t failed_address, failed_command;
    bool transfer_failed, failed_read, quarantined;
} sonar_motor_zynq_diagnostic_t;

bool sonar_motor_zynq_init(sonar_tic_io_t *io);
/* Snapshot only: does not issue I2C commands or touch a clock-gated controller.
 * The first failed transfer is retained even if shutdown transfers also fail. */
const sonar_motor_zynq_diagnostic_t *sonar_motor_zynq_diagnostic(void);
#endif
