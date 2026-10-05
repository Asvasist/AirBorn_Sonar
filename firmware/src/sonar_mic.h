#ifndef SONAR_MIC_H
#define SONAR_MIC_H

#include <stdbool.h>
#include <stdint.h>

#define SONAR_MIC_CACHE_LINE 32U
#define SONAR_MIC_MAX_DMA_BYTES UINT32_C(0x03ffffff)
#define SONAR_MIC_IRQ_DONE     UINT32_C(1)
#define SONAR_MIC_IRQ_ERROR    UINT32_C(2)
#define SONAR_MIC_IRQ_OVERFLOW UINT32_C(4)

typedef enum { MIC_IDLE, MIC_CAPTURING, MIC_READY, MIC_FAULT } sonar_mic_state_t;
typedef enum {
    MIC_OK, MIC_BAD_CONFIG, MIC_PREPARE_FAILED, MIC_ARM_FAILED,
    MIC_TRIGGER_FAILED, MIC_DMA_ERROR, MIC_OVERFLOW, MIC_BAD_LENGTH,
    MIC_TIMEOUT, MIC_FINISH_FAILED, MIC_CANCELLED
} sonar_mic_fault_t;

typedef struct {
    uint32_t bytes;
    uint32_t timeout_ticks;
    uint32_t pdm_hz;
} sonar_mic_config_t;

/* All callbacks run in the owning task, never in the ISR.
 * prepare: clear old events and flush isolated buffer cache lines.
 * arm: give the buffer to DMA; do not generate a capture edge.
 * trigger: generate one rising edge and return the line low.
 * finish: prove DMA is idle, then invalidate the isolated cache span.
 * abort: stop/reset DMA within a bound; true means DMA no longer owns memory.
 */
typedef struct {
    void *context;
    bool (*prepare)(void *, uint8_t *, uint32_t);
    bool (*arm)(void *, uint8_t *, uint32_t, uint32_t);
    bool (*trigger)(void *);
    bool (*finish)(void *, uint8_t *, uint32_t);
    bool (*abort)(void *);
} sonar_mic_io_t;

typedef struct {
    uint32_t generation;
    uint32_t flags;
    uint32_t bytes;
    uint32_t tick;
    bool overflow_observable;
} sonar_mic_event_t;

typedef struct {
    const uint8_t *data;
    uint32_t bytes;
    uint32_t generation;
    uint32_t request_tick;
    uint32_t completion_tick;
    uint32_t pdm_hz;
    bool overflow_checked;
} sonar_mic_frame_t;

/* One task owns the complete object. ISR events are delivered by value. */
typedef struct {
    sonar_mic_state_t state;
    sonar_mic_fault_t fault;
    sonar_mic_config_t config;
    sonar_mic_io_t io;
    uint8_t *buffer;
    uint32_t cache_span;
    uint32_t generation;
    uint32_t started;
    uint32_t completed;
    bool dma_owned;
    bool overflow_checked;
} sonar_mic_t;

bool sonar_mic_init(sonar_mic_t *capture, const sonar_mic_config_t *config,
                    const sonar_mic_io_t *io, uint8_t *buffer, uint32_t capacity);
bool sonar_mic_start(sonar_mic_t *capture, uint32_t now);
void sonar_mic_event(sonar_mic_t *capture, const sonar_mic_event_t *event, uint32_t now);
void sonar_mic_poll(sonar_mic_t *capture, uint32_t now);
void sonar_mic_cancel(sonar_mic_t *capture);
/* The view remains valid until release. No transfer may start while it is held. */
bool sonar_mic_frame(const sonar_mic_t *capture, sonar_mic_frame_t *frame);
bool sonar_mic_release(sonar_mic_t *capture);
const char *sonar_mic_fault_name(sonar_mic_fault_t fault);

#endif
