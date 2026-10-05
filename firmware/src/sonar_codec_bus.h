#ifndef SONAR_CODEC_BUS_H
#define SONAR_CODEC_BUS_H
#include <stdbool.h>
#include <stdint.h>

/* AXI IIC dynamic-mode write transport for the two-byte codec protocol. */
enum { CODEC_GIER = 0x1c, CODEC_ISR = 0x20, CODEC_IER = 0x28,
    CODEC_RESET = 0x40, CODEC_CR = 0x100, CODEC_SR = 0x104,
    CODEC_TX = 0x108, CODEC_RX_DEPTH = 0x120 };
typedef struct {
    void *context;
    uint32_t (*read)(void *context, uint32_t offset);
    void (*write)(void *context, uint32_t offset, uint32_t value);
} sonar_codec_bus_io_t;
typedef struct {
    sonar_codec_bus_io_t io;
    uint32_t token;
    uint8_t bytes[2];
    bool active, waiting_for_busy, terminal, quarantined;
} sonar_codec_bus_t;
typedef struct { uint32_t token; bool acknowledged; } sonar_codec_event_t;
bool sonar_codec_bus_init(sonar_codec_bus_t *bus, const sonar_codec_bus_io_t *io);
bool sonar_codec_bus_write(sonar_codec_bus_t *bus, uint8_t address, const uint8_t bytes[2], uint32_t token);
/* Nonblocking. The speaker service owns the transaction deadline. */
bool sonar_codec_bus_event(sonar_codec_bus_t *bus, sonar_codec_event_t *event);
bool sonar_codec_bus_abort(sonar_codec_bus_t *bus);

/* One owner maintains the complete GPIO output word. Changes to codec enable
 * must preserve the shared trigger. No independent read/modify/write owners. */
bool sonar_gpio_merge(uint32_t shadow, uint32_t mask, uint32_t value, uint32_t *result);
#endif
