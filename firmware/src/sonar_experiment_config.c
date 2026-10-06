#include "sonar_experiment_config.h"
#include <stddef.h>

bool sonar_experiment_calculate(const sonar_experiment_config_t *c, uint32_t wav_samples,
                                sonar_chirp_values_t *out)
{
    sonar_chirp_values_t v = {0};
    if (c == NULL || out == NULL || c->amplitude_pct > 100U) {
        return false;
    }
    if (c->mode == SONAR_TX_GENERATE) {
        if (c->start_hz == 0U || c->stop_hz == 0U || c->start_hz >= SONAR_AUDIO_HZ / 2U ||
            c->stop_hz >= SONAR_AUDIO_HZ / 2U || c->duration_us == 0U ||
            c->duration_us > SONAR_CAPTURE_US - SONAR_TX_START_MARGIN_US) {
            return false;
        }
        v.total_samples =
            (uint32_t)(((uint64_t)c->duration_us * SONAR_AUDIO_HZ + 500000U) / 1000000U);
        if (v.total_samples < 2U || v.total_samples > SONAR_TX_MAX_SAMPLES) {
            return false;
        }
        v.start_phase_inc =
            (uint32_t)((((uint64_t)c->start_hz << 32U) + SONAR_AUDIO_HZ / 2U) / SONAR_AUDIO_HZ);
        uint32_t stop =
            (uint32_t)((((uint64_t)c->stop_hz << 32U) + SONAR_AUDIO_HZ / 2U) / SONAR_AUDIO_HZ);
        int64_t delta = (int64_t)stop - (int64_t)v.start_phase_inc;
        int64_t denominator = (int64_t)v.total_samples - 1;
        int64_t step = delta >= 0 ? (delta + denominator / 2) / denominator
                                  : -((-delta + denominator / 2) / denominator);
        if (step < INT32_MIN || step > INT32_MAX) {
            return false;
        }
        v.phase_inc_step = (int32_t)step;
    } else if (c->mode == SONAR_TX_WAV) {
        if (c->waveform_id == 0U || wav_samples == 0U || wav_samples > SONAR_TX_MAX_SAMPLES) {
            return false;
        }
        v.total_samples = wav_samples;
    } else {
        return false;
    }
    v.amplitude = (c->amplitude_pct * 32767U + 50U) / 100U;
    v.duration_us =
        (uint32_t)(((uint64_t)v.total_samples * 1000000U + SONAR_AUDIO_HZ - 1U) / SONAR_AUDIO_HZ);
    if (v.duration_us + SONAR_TX_START_MARGIN_US > SONAR_CAPTURE_US) {
        return false;
    }
    *out = v;
    return true;
}
