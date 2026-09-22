#include "sonar_export.h"
#include <stddef.h>
#include <string.h>

static void put16(uint8_t *p, uint16_t n)
{
    p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8);
}
static void put32(uint8_t *p, uint32_t n)
{
    for (unsigned i = 0U; i < 4U; ++i) { p[i] = (uint8_t)(n >> (8U * i)); }
}
static uint32_t crc_update(uint32_t crc, const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0U; i < n; ++i) {
        crc ^= p[i];
        for (unsigned b = 0U; b < 8U; ++b) {
            crc = (crc >> 1) ^ ((crc & 1U) != 0U ? 0xedb88320U : 0U);
        }
    }
    return crc;
}
bool sonar_export_active(const sonar_export_t *e)
{
    return e != NULL && (e->state == EXPORT_HEADER || e->state == EXPORT_DATA ||
                         e->state == EXPORT_END);
}
bool sonar_export_init(sonar_export_t *e, const sonar_export_io_t *io)
{
    if (e == NULL || io == NULL || io->write == NULL) { return false; }
    memset(e, 0, sizeof(*e));
    e->io = *io;
    return true;
}
bool sonar_export_begin(sonar_export_t *e, const sonar_mic_frame_t *frame, uint32_t tick_hz)
{
    if (e == NULL || e->io.write == NULL || sonar_export_active(e) || frame == NULL ||
        frame->data == NULL || frame->bytes == 0U || frame->bytes % 4U != 0U ||
        frame->bytes > SONAR_MIC_MAX_DMA_BYTES || frame->pdm_hz == 0U || tick_hz == 0U) {
        return false;
    }
    e->frame = *frame;
    e->tick_hz = tick_hz;
    if (++e->transfer_id == 0U) { e->transfer_id = 1U; }
    e->offset = 0U;
    e->crc = 0xffffffffU;
    e->state = EXPORT_HEADER;
    return true;
}
void sonar_export_poll(sonar_export_t *e)
{
    uint8_t packet[SONAR_EXPORT_PACKET_MAX] = {0};
    uint8_t *payload = packet + SONAR_EXPORT_HEADER;
    uint32_t n;
    uint8_t kind;
    if (!sonar_export_active(e)) { return; }
    if (e->state == EXPORT_HEADER) {
        kind = 1U; n = 24U;
        put32(payload, e->frame.generation);
        put32(payload + 4, e->frame.pdm_hz);
        put32(payload + 8, e->frame.request_tick);
        put32(payload + 12, e->frame.completion_tick);
        put32(payload + 16, e->tick_hz);
        /* Bit 0: MSB-first PDM. Bit 1: overflow checked. Bit 2: LE words. */
        put32(payload + 20, e->frame.overflow_checked ? 7U : 5U);
    } else if (e->state == EXPORT_DATA) {
        kind = 2U; n = e->frame.bytes - e->offset;
        if (n > SONAR_EXPORT_CHUNK) { n = SONAR_EXPORT_CHUNK; }
        memcpy(payload, e->frame.data + e->offset, n);
    } else {
        kind = 3U; n = 4U;
        put32(payload, e->crc ^ 0xffffffffU);
    }
    memcpy(packet, "ASPD", 4U);
    packet[4] = 1U; packet[5] = kind;
    put16(packet + 6, SONAR_EXPORT_HEADER);
    put32(packet + 8, e->transfer_id);
    put32(packet + 12, e->offset);
    put32(packet + 16, e->frame.bytes);
    put16(packet + 20, (uint16_t)n);
    put32(payload + n, crc_update(0xffffffffU, packet, SONAR_EXPORT_HEADER + n) ^ 0xffffffffU);
    if (!e->io.write(e->io.context, packet, SONAR_EXPORT_HEADER + n + 4U)) {
        e->state = EXPORT_FAILED;
    } else if (e->state == EXPORT_HEADER) {
        e->state = EXPORT_DATA;
    } else if (e->state == EXPORT_DATA) {
        e->crc = crc_update(e->crc, payload, n);
        e->offset += n;
        if (e->offset == e->frame.bytes) { e->state = EXPORT_END; }
    } else {
        e->state = EXPORT_DONE;
    }
}
void sonar_export_cancel(sonar_export_t *e)
{
    if (sonar_export_active(e)) { e->state = EXPORT_CANCELLED; }
}
