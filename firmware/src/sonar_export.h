#ifndef SONAR_EXPORT_H
#define SONAR_EXPORT_H
#include "sonar_mic.h"
#define SONAR_EXPORT_CHUNK 128U
#define SONAR_EXPORT_HEADER 24U
#define SONAR_EXPORT_PACKET_MAX (SONAR_EXPORT_HEADER + SONAR_EXPORT_CHUNK + 4U)
typedef enum { EXPORT_IDLE, EXPORT_HEADER, EXPORT_DATA, EXPORT_END,
    EXPORT_DONE, EXPORT_FAILED, EXPORT_CANCELLED } sonar_export_state_t;
typedef struct {
    void *context;
    /* Send one complete packet without interleaving diagnostic bytes. */
    bool (*write)(void *context, const uint8_t *packet, uint32_t bytes);
} sonar_export_io_t;
typedef struct {
    sonar_export_io_t io;
    sonar_mic_frame_t frame;
    sonar_export_state_t state;
    uint32_t transfer_id, offset, crc, tick_hz;
} sonar_export_t;
/* Caller owns the capture lease until DONE/FAILED/CANCELLED. One task owns
 * this object; poll sends at most one packet and never allocates memory. */
bool sonar_export_init(sonar_export_t *exporter, const sonar_export_io_t *io);
bool sonar_export_begin(sonar_export_t *exporter, const sonar_mic_frame_t *frame, uint32_t tick_hz);
bool sonar_export_active(const sonar_export_t *exporter);
void sonar_export_poll(sonar_export_t *exporter);
void sonar_export_cancel(sonar_export_t *exporter);
#endif
