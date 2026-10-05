#include "sonar_ps_i2c.h"
#include <stddef.h>
#include <string.h>

static uint32_t read_reg(sonar_ps_i2c_t *bus, uint32_t offset)
{ return bus->io.read(bus->io.context, offset); }
static void write_reg(sonar_ps_i2c_t *bus, uint32_t offset, uint32_t value)
{ bus->io.write(bus->io.context, offset, value); }
static uint32_t now(sonar_ps_i2c_t *bus) { return bus->io.now(bus->io.context); }

bool sonar_ps_i2c_init(sonar_ps_i2c_t *bus, const sonar_ps_i2c_io_t *io,
    uint32_t input_hz, uint32_t scl_hz, uint32_t timeout_ticks)
{
    uint32_t divider = 0U, best_error = UINT32_MAX;
    if (bus == NULL || io == NULL || io->read == NULL || io->write == NULL ||
        io->now == NULL || io->wait == NULL || input_hz == 0U || scl_hz == 0U ||
        scl_hz > 400000U || timeout_ticks == 0U || timeout_ticks >= UINT32_C(0x80000000)) { return false; }
    for (uint32_t a = 0U; a < 4U; ++a) {
        for (uint32_t b = 0U; b < 64U; ++b) {
            uint32_t actual = input_hz / (22U * (a + 1U) * (b + 1U));
            uint32_t error = actual > scl_hz ? actual - scl_hz : scl_hz - actual;
            if (error < best_error) { best_error = error; divider = (a << 14U) | (b << 8U); }
        }
    }
    if (best_error > scl_hz / 20U || (io->read(io->context, PS_I2C_SR) & PS_I2C_BUSY) != 0U) { return false; }
    *bus = (sonar_ps_i2c_t){.io = *io, .divider = divider, .timeout_ticks = timeout_ticks};
    write_reg(bus, PS_I2C_IDR, PS_I2C_ALL_IRQ);
    write_reg(bus, PS_I2C_CR, divider | 0x40U);
    write_reg(bus, PS_I2C_ISR, PS_I2C_ALL_IRQ);
    write_reg(bus, PS_I2C_TIMEOUT, 0xffU);
    return true;
}

static bool abort_bus(sonar_ps_i2c_t *bus)
{
    uint32_t start = now(bus);
    /* Release HOLD and master mode; FIFO clear is not proof of an idle bus. */
    write_reg(bus, PS_I2C_CR, bus->divider | 0x40U);
    write_reg(bus, PS_I2C_ISR, PS_I2C_ALL_IRQ);
    while ((read_reg(bus, PS_I2C_SR) & PS_I2C_BUSY) != 0U) {
        if ((uint32_t)(now(bus) - start) >= bus->timeout_ticks) { bus->quarantined = true; break; }
        bus->io.wait(bus->io.context);
    }
    return false;
}

static bool transfer(sonar_ps_i2c_t *bus, uint8_t address, uint8_t *data, uint32_t length, bool reading)
{
    uint32_t start, received = 0U;
    if (bus == NULL || data == NULL || address < 8U || address > 119U ||
        length == 0U || length > 16U || bus->quarantined) { return false; }
    start = now(bus);
    while ((read_reg(bus, PS_I2C_SR) & PS_I2C_BUSY) != 0U) {
        if ((uint32_t)(now(bus) - start) >= bus->timeout_ticks) { return abort_bus(bus); }
        bus->io.wait(bus->io.context);
    }
    /* ACK, 7-bit addressing, master, FIFO clear; no HOLD or slave monitor. */
    write_reg(bus, PS_I2C_CR, bus->divider | 0x4eU | (reading ? 1U : 0U));
    write_reg(bus, PS_I2C_ISR, PS_I2C_ALL_IRQ);
    bus->last_irq = 0U;
    if (reading) { write_reg(bus, PS_I2C_SIZE, length); }
    else {
        for (uint32_t i = 0U; i < length; ++i) { write_reg(bus, PS_I2C_DATA, data[i]); }
    }
    /* Address write issues START. The address is seven bits, not shifted. */
    write_reg(bus, PS_I2C_ADDR, address);
    for (;;) {
        bus->last_irq = read_reg(bus, PS_I2C_ISR);
        if ((bus->last_irq & PS_I2C_ERRORS) != 0U) { return abort_bus(bus); }
        if ((uint32_t)(now(bus) - start) >= bus->timeout_ticks) { return abort_bus(bus); }
        if (reading) {
            while ((read_reg(bus, PS_I2C_SR) & PS_I2C_RX_VALID) != 0U) {
                if (received >= length) { return abort_bus(bus); }
                data[received++] = (uint8_t)read_reg(bus, PS_I2C_DATA);
            }
        }
        if ((bus->last_irq & PS_I2C_COMPLETE) != 0U && (read_reg(bus, PS_I2C_SR) & PS_I2C_BUSY) == 0U) {
            if (reading && received != length) { return abort_bus(bus); }
            write_reg(bus, PS_I2C_ISR, PS_I2C_ALL_IRQ);
            return true;
        }
        bus->io.wait(bus->io.context);
    }
}

bool sonar_ps_i2c_write(sonar_ps_i2c_t *bus, uint8_t address, const uint8_t *data, uint32_t length)
{
    uint8_t copy[16];
    if (data == NULL || length == 0U || length > sizeof(copy)) { return false; }
    memcpy(copy, data, length);
    return transfer(bus, address, copy, length, false);
}

bool sonar_ps_i2c_read(sonar_ps_i2c_t *bus, uint8_t address, uint8_t *data, uint32_t length)
{
    uint8_t copy[16];
    if (data == NULL || length == 0U || length > sizeof(copy)) { return false; }
    if (!transfer(bus, address, copy, length, true)) { return false; }
    memcpy(data, copy, length);
    return true;
}
