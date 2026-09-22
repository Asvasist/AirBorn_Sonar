#include "sonar_speaker.h"
#include "sonar_config.h"
#include <stddef.h>

/* Register/value pairs from the ZIP's ssm2603_init_i2c.v. Volume is supplied
 * separately; waveform data, I2S generation and repeats remain FPGA-owned. */
static const struct { uint8_t reg; uint16_t value; } sequence[SONAR_CODEC_WRITES] = {
    {0x0fU, 0x000U}, {0x06U, 0x077U}, {0x02U, 0x17fU}, {0x03U, 0x17fU},
    {0x04U, 0x010U}, {0x05U, 0x000U}, {0x07U, 0x002U}, {0x08U, 0x000U},
    {0x09U, 0x001U}, {0x06U, 0x067U}
};

static bool valid_ticks(uint32_t ticks) { return ticks != 0U && ticks < UINT32_C(0x80000000); }

bool sonar_speaker_default_config(uint32_t tick_hz, sonar_speaker_config_t *config)
{
    sonar_speaker_config_t value = {0U, 0U, 0U, 127U};
    if (config == NULL || !sonar_ms_to_ticks(10U, tick_hz, &value.powerup_ticks) ||
        !sonar_ms_to_ticks(80U, tick_hz, &value.vmid_ticks) ||
        !sonar_ms_to_ticks(100U, tick_hz, &value.write_timeout_ticks)) { return false; }
    *config = value;
    return true;
}

bool sonar_speaker_encode(uint8_t reg, uint16_t value, uint8_t bytes[2])
{
    if (bytes == NULL || reg > 127U || value > 511U) { return false; }
    bytes[0] = (uint8_t)(((uint16_t)reg << 1U) | (value >> 8U));
    bytes[1] = (uint8_t)(value & 0xffU);
    return true;
}

bool sonar_speaker_init(sonar_speaker_t *speaker, const sonar_speaker_config_t *config,
                        const sonar_speaker_io_t *io)
{
    if (speaker == NULL || config == NULL || io == NULL || io->write == NULL ||
        io->abort == NULL || io->enable == NULL || config->volume > 127U ||
        !valid_ticks(config->powerup_ticks) || !valid_ticks(config->vmid_ticks) ||
        !valid_ticks(config->write_timeout_ticks)) { return false; }
    *speaker = (sonar_speaker_t){.io = *io, .config = *config,
        .state = SPEAKER_OFF, .fault = SPEAKER_OK};
    return true;
}

static bool mute(sonar_speaker_t *speaker)
{
    speaker->mute_confirmed = speaker->io.enable(speaker->io.context, false);
    if (speaker->mute_confirmed) { speaker->output_enabled = false; }
    return speaker->mute_confirmed;
}

static void fail(sonar_speaker_t *speaker, sonar_speaker_fault_t fault)
{
    speaker->state = SPEAKER_FAULT;
    speaker->fault = fault;
    (void)mute(speaker);
    if (speaker->bus_owned && speaker->io.abort(speaker->io.context)) { speaker->bus_owned = false; }
}

bool sonar_speaker_begin(sonar_speaker_t *speaker, uint32_t now)
{
    if (speaker == NULL || speaker->state != SPEAKER_OFF) { return false; }
    if (!mute(speaker)) {
        speaker->state = SPEAKER_FAULT;
        speaker->fault = SPEAKER_MUTE_FAILED;
        return false;
    }
    speaker->state = SPEAKER_CONFIGURING;
    speaker->phase = SPEAKER_WAIT_POWER;
    speaker->phase_tick = now;
    return true;
}

static void issue_write(sonar_speaker_t *speaker, uint32_t now)
{
    uint8_t bytes[2];
    uint16_t value = sequence[speaker->index].value;
    if (speaker->index == 2U || speaker->index == 3U) { value = (uint16_t)(0x100U | speaker->config.volume); }
    (void)sonar_speaker_encode(sequence[speaker->index].reg, value, bytes);
    ++speaker->token;
    if (speaker->token == 0U) { ++speaker->token; }
    speaker->phase = SPEAKER_WAIT_ACK;
    speaker->phase_tick = now;
    speaker->bus_owned = true;
    if (!speaker->io.write(speaker->io.context, SONAR_CODEC_ADDRESS, bytes, speaker->token)) {
        fail(speaker, SPEAKER_WRITE_FAILED);
    }
}

void sonar_speaker_poll(sonar_speaker_t *speaker, uint32_t now)
{
    if (speaker == NULL || speaker->state != SPEAKER_CONFIGURING) { return; }
    uint32_t elapsed = now - speaker->phase_tick;
    if (speaker->phase == SPEAKER_WAIT_ACK) {
        if (elapsed >= speaker->config.write_timeout_ticks) { fail(speaker, SPEAKER_TIMEOUT); }
    } else {
        uint32_t delay = speaker->phase == SPEAKER_WAIT_POWER ? speaker->config.powerup_ticks : speaker->config.vmid_ticks;
        if (elapsed >= delay) { issue_write(speaker, now); }
    }
}

void sonar_speaker_event(sonar_speaker_t *speaker, uint32_t token, bool acknowledged, uint32_t now)
{
    if (speaker == NULL || speaker->state != SPEAKER_CONFIGURING || speaker->phase != SPEAKER_WAIT_ACK) { return; }
    sonar_speaker_poll(speaker, now);
    if (speaker->state != SPEAKER_CONFIGURING || token != speaker->token) { return; }
    if (!acknowledged) { fail(speaker, SPEAKER_BUS_ERROR); return; }
    speaker->bus_owned = false;
    ++speaker->index;
    if (speaker->index == SONAR_CODEC_WRITES) { speaker->state = SPEAKER_READY; }
    else if (speaker->index == 8U) {
        speaker->phase = SPEAKER_WAIT_VMID;
        speaker->phase_tick = now;
    } else { issue_write(speaker, now); }
}

bool sonar_speaker_enable(sonar_speaker_t *speaker, bool enabled)
{
    if (speaker == NULL || speaker->state != SPEAKER_READY) { return false; }
    if (speaker->output_enabled == enabled) { return true; }
    if (!speaker->io.enable(speaker->io.context, enabled)) {
        fail(speaker, SPEAKER_MUTE_FAILED);
        return false;
    }
    speaker->output_enabled = enabled;
    speaker->mute_confirmed = !enabled;
    return true;
}

void sonar_speaker_cancel(sonar_speaker_t *speaker)
{
    if (speaker != NULL && (speaker->state == SPEAKER_CONFIGURING || speaker->state == SPEAKER_READY)) {
        fail(speaker, SPEAKER_CANCELLED);
    }
}
