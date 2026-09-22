#include "sonar_mic.h"
#include <stddef.h>

bool sonar_mic_init(sonar_mic_t *capture, const sonar_mic_config_t *config,
                    const sonar_mic_io_t *io, uint8_t *buffer, uint32_t capacity)
{
    uint32_t span;
    if (capture == NULL) { return false; }
    *capture = (sonar_mic_t){0};
    capture->state = MIC_FAULT;
    capture->fault = MIC_BAD_CONFIG;
    if (config == NULL || io == NULL || buffer == NULL ||
        io->prepare == NULL || io->arm == NULL || io->trigger == NULL ||
        io->finish == NULL || io->abort == NULL || config->bytes == 0U ||
        config->bytes > SONAR_MIC_MAX_DMA_BYTES || config->bytes % 4U != 0U ||
        config->timeout_ticks == 0U || config->timeout_ticks > UINT32_MAX / 2U ||
        config->pdm_hz == 0U || (uintptr_t)buffer % SONAR_MIC_CACHE_LINE != 0U) {
        return false;
    }
    span = (config->bytes + SONAR_MIC_CACHE_LINE - 1U) & ~(SONAR_MIC_CACHE_LINE - 1U);
    if (capacity < span) { return false; }
    capture->config = *config;
    capture->io = *io;
    capture->buffer = buffer;
    capture->cache_span = span;
    capture->state = MIC_IDLE;
    capture->fault = MIC_OK;
    return true;
}

static void fail(sonar_mic_t *capture, sonar_mic_fault_t cause)
{
    if (capture->state == MIC_FAULT) { return; }
    capture->state = MIC_FAULT;
    capture->fault = cause;
    if (capture->dma_owned && capture->io.abort(capture->io.context)) {
        capture->dma_owned = false;
    }
    /* A reset DMA does not reset the PL packer. Faults require a board reset. */
}

bool sonar_mic_start(sonar_mic_t *capture, uint32_t now)
{
    if (capture == NULL || capture->state != MIC_IDLE) { return false; }
    capture->started = now;
    capture->overflow_checked = false;
    ++capture->generation;
    if (!capture->io.prepare(capture->io.context, capture->buffer, capture->cache_span)) {
        fail(capture, MIC_PREPARE_FAILED);
        return false;
    }
    capture->state = MIC_CAPTURING;
    /* An arm failure may follow partial register writes: assume DMA owns memory. */
    capture->dma_owned = true;
    if (!capture->io.arm(capture->io.context, capture->buffer, capture->config.bytes,
                          capture->generation)) {
        fail(capture, MIC_ARM_FAILED);
        return false;
    }
    if (!capture->io.trigger(capture->io.context)) {
        fail(capture, MIC_TRIGGER_FAILED);
        return false;
    }
    return true;
}

void sonar_mic_event(sonar_mic_t *capture, const sonar_mic_event_t *event, uint32_t now)
{
    if (capture == NULL || event == NULL || capture->state != MIC_CAPTURING ||
        event->generation != capture->generation) { return; }
    if ((event->flags & SONAR_MIC_IRQ_ERROR) != 0U) { fail(capture, MIC_DMA_ERROR); }
    else if ((event->flags & SONAR_MIC_IRQ_OVERFLOW) != 0U) { fail(capture, MIC_OVERFLOW); }
    else if ((event->flags & SONAR_MIC_IRQ_DONE) != 0U) {
        if ((uint32_t)(now - capture->started) >= capture->config.timeout_ticks ||
            (uint32_t)(event->tick - capture->started) >= capture->config.timeout_ticks ||
            (uint32_t)(now - event->tick) > UINT32_MAX / 2U) {
            fail(capture, MIC_TIMEOUT);
        } else if (event->bytes != capture->config.bytes) {
            fail(capture, MIC_BAD_LENGTH);
        } else if (!capture->io.finish(capture->io.context, capture->buffer, capture->cache_span)) {
            fail(capture, MIC_FINISH_FAILED);
        } else {
            capture->dma_owned = false;
            capture->completed = event->tick;
            capture->overflow_checked = event->overflow_observable;
            capture->state = MIC_READY;
        }
    }
}

void sonar_mic_poll(sonar_mic_t *capture, uint32_t now)
{
    if (capture != NULL && capture->state == MIC_CAPTURING &&
        (uint32_t)(now - capture->started) >= capture->config.timeout_ticks) {
        fail(capture, MIC_TIMEOUT);
    }
}
void sonar_mic_cancel(sonar_mic_t *capture)
{
    if (capture != NULL && capture->state == MIC_CAPTURING) { fail(capture, MIC_CANCELLED); }
}
bool sonar_mic_frame(const sonar_mic_t *capture, sonar_mic_frame_t *frame)
{
    if (capture == NULL || frame == NULL || capture->state != MIC_READY || capture->dma_owned) {
        return false;
    }
    *frame = (sonar_mic_frame_t){capture->buffer, capture->config.bytes, capture->generation,
        capture->started, capture->completed, capture->config.pdm_hz, capture->overflow_checked};
    return true;
}
bool sonar_mic_release(sonar_mic_t *capture)
{
    if (capture == NULL || capture->state != MIC_READY || capture->dma_owned) { return false; }
    capture->state = MIC_IDLE;
    return true;
}
const char *sonar_mic_fault_name(sonar_mic_fault_t fault)
{
    switch (fault) {
    case MIC_OK: return "OK";
    case MIC_BAD_CONFIG: return "CONFIG";
    case MIC_PREPARE_FAILED: return "PREPARE";
    case MIC_ARM_FAILED: return "DMA_ARM";
    case MIC_TRIGGER_FAILED: return "TRIGGER";
    case MIC_DMA_ERROR: return "DMA_ERROR";
    case MIC_OVERFLOW: return "PL_OVERFLOW";
    case MIC_BAD_LENGTH: return "FRAME_LENGTH";
    case MIC_TIMEOUT: return "TIMEOUT";
    case MIC_FINISH_FAILED: return "DMA_FINISH";
    case MIC_CANCELLED: return "CANCELLED";
    default: return "UNKNOWN";
    }
}
