#include "sonar_codec_bus.h"
#include <stddef.h>

static uint32_t read_reg(sonar_codec_bus_t *bus, uint32_t offset)
{ return bus->io.read(bus->io.context, offset); }
static void write_reg(sonar_codec_bus_t *bus, uint32_t offset, uint32_t value)
{ bus->io.write(bus->io.context, offset, value); }
static void clear_irq(sonar_codec_bus_t *bus, uint32_t mask)
{
    /* AXI IIC IISR is toggle-on-write, unlike the PS I2C W1C register. */
    uint32_t pending = read_reg(bus, CODEC_ISR) & mask;
    if (pending != 0U) { write_reg(bus, CODEC_ISR, pending); }
}
static bool reset_bus(sonar_codec_bus_t *bus)
{
    write_reg(bus, CODEC_RESET, 0x0aU);
    write_reg(bus, CODEC_GIER, 0U);
    write_reg(bus, CODEC_IER, 0U);
    write_reg(bus, CODEC_RX_DEPTH, 15U);
    write_reg(bus, CODEC_CR, 2U);
    write_reg(bus, CODEC_CR, 1U);
    return read_reg(bus, CODEC_SR) == 0xc0U;
}
bool sonar_codec_bus_init(sonar_codec_bus_t *bus, const sonar_codec_bus_io_t *io)
{
    if (bus == NULL || io == NULL || io->read == NULL || io->write == NULL) { return false; }
    *bus = (sonar_codec_bus_t){.io = *io};
    bus->quarantined = !reset_bus(bus);
    return !bus->quarantined;
}
bool sonar_codec_bus_write(sonar_codec_bus_t *bus, uint8_t address, const uint8_t bytes[2], uint32_t token)
{
    if (bus == NULL || bytes == NULL || token == 0U || address < 8U || address > 119U ||
        bus->active || bus->quarantined || read_reg(bus, CODEC_SR) != 0xc0U) { return false; }
    bus->bytes[0] = bytes[0]; bus->bytes[1] = bytes[1]; bus->token = token;
    bus->active = true; bus->terminal = false; bus->waiting_for_busy = true;
    clear_irq(bus, 0x1fU);
    write_reg(bus, CODEC_TX, 0x100U | ((uint32_t)address << 1U));
    return true;
}
bool sonar_codec_bus_event(sonar_codec_bus_t *bus, sonar_codec_event_t *event)
{
    if (bus == NULL || event == NULL || !bus->active || bus->terminal) { return false; }
    uint32_t irq = read_reg(bus, CODEC_ISR), status = read_reg(bus, CODEC_SR);
    if ((irq & 3U) != 0U) {
        bus->terminal = true;
        *event = (sonar_codec_event_t){bus->token, false};
        return true;
    }
    if (bus->waiting_for_busy) {
        if ((status & 4U) == 0U) { return false; }
        /* Clear stale BNB only after START made the bus busy. With no STOP
         * queued yet, a short transaction cannot race this observation. */
        clear_irq(bus, 0x10U);
        write_reg(bus, CODEC_TX, bus->bytes[0]);
        write_reg(bus, CODEC_TX, 0x200U | bus->bytes[1]);
        bus->waiting_for_busy = false;
        return false;
    }
    if ((irq & 0x10U) != 0U && (status & 0x84U) == 0x80U) {
        bus->active = false;
        *event = (sonar_codec_event_t){bus->token, true};
        return true;
    }
    return false;
}
bool sonar_codec_bus_abort(sonar_codec_bus_t *bus)
{
    if (bus == NULL || bus->quarantined) { return false; }
    if (!reset_bus(bus)) { bus->quarantined = true; return false; }
    bus->active = false; bus->terminal = false;
    return true;
}
bool sonar_gpio_merge(uint32_t shadow, uint32_t mask, uint32_t value, uint32_t *result)
{
    if (result == NULL || (shadow & ~3U) != 0U || (mask & ~3U) != 0U || (value & ~mask) != 0U) { return false; }
    *result = (shadow & ~mask) | value;
    return true;
}
