#ifndef SONAR_EXPERIMENT_CONFIG_H
#define SONAR_EXPERIMENT_CONFIG_H
#include <stdbool.h>
#include <stdint.h>

#define SONAR_AUDIO_HZ 96000U /* Fixed speaker TX rate; RX PCM is also 96 kHz. */
#define SONAR_CAPTURE_US 50000U
#define SONAR_TX_START_MARGIN_US 1000U
#define SONAR_TX_MAX_SAMPLES 4704U /* 49 ms; reserve 1 ms inside fixed RX. */
#define SONAR_TX_MAX_BYTES (SONAR_TX_MAX_SAMPLES * 2U)
#define SONAR_WAV_FILE_MAX_BYTES 65536U

typedef enum { SONAR_TX_GENERATE = 0, SONAR_TX_WAV = 1 } sonar_tx_mode_t;
typedef struct {
    sonar_tx_mode_t mode;
    uint32_t start_hz, stop_hz, duration_us, amplitude_pct;
    uint32_t waveform_id, config_id;
} sonar_experiment_config_t;
typedef struct {
    uint32_t start_phase_inc;
    int32_t phase_inc_step;
    uint32_t total_samples, amplitude, duration_us;
} sonar_chirp_values_t;

/* Pure calculation. Output is unchanged on failure. WAV count comes from a
 * verified waveform, never the host's duration field. Signed steps round to
 * nearest, with ties away from zero. Duration is rounded UP to whole us. */
bool sonar_experiment_calculate(const sonar_experiment_config_t *config,
                               uint32_t verified_wav_samples,
                               sonar_chirp_values_t *values);
#endif
