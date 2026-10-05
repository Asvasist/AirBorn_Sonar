#ifndef SONAR_PS_I2C_H
#define SONAR_PS_I2C_H
#include <stdbool.h>
#include <stdint.h>

/* Zynq-7000 PS I2C register offsets/masks, matching AMD xiicps_hw.h. */
enum { PS_I2C_CR = 0x00, PS_I2C_SR = 0x04, PS_I2C_ADDR = 0x08,
    PS_I2C_DATA = 0x0c, PS_I2C_ISR = 0x10, PS_I2C_SIZE = 0x14,
    PS_I2C_TIMEOUT = 0x1c, PS_I2C_IDR = 0x28 };
#define PS_I2C_BUSY UINT32_C(0x100)
#define PS_I2C_RX_VALID UINT32_C(0x20)
#define PS_I2C_COMPLETE UINT32_C(1)
#define PS_I2C_ERRORS UINT32_C(0x2ec)
#define PS_I2C_ALL_IRQ UINT32_C(0x2ff)

typedef struct {
    void *context;
    uint32_t (*read)(void *context, uint32_t offset);
    void (*write)(void *context, uint32_t offset, uint32_t value);
    uint32_t (*now)(void *context);
    void (*wait)(void *context); /* Yield one tick; time must continue advancing. */
} sonar_ps_i2c_io_t;
typedef struct {
    sonar_ps_i2c_io_t io;
    uint32_t divider, timeout_ticks, last_irq;
    bool quarantined;
} sonar_ps_i2c_t;

/* Single task owner. Transfers are limited to the 16-byte FIFO and yield while
 * pending. No automatic retries of commands with uncertain side effects. */
bool sonar_ps_i2c_init(sonar_ps_i2c_t *bus, const sonar_ps_i2c_io_t *io,
    uint32_t input_hz, uint32_t scl_hz, uint32_t timeout_ticks);
bool sonar_ps_i2c_write(sonar_ps_i2c_t *bus, uint8_t address, const uint8_t *data, uint32_t length);
bool sonar_ps_i2c_read(sonar_ps_i2c_t *bus, uint8_t address, uint8_t *data, uint32_t length);
#endif
