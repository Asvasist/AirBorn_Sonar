#include "sonar_chirp.h"
#include "sonar_audio_hw.h"
#include "sonar_platform.h"
#include "sonar_pl_gpio.h"
#include "sonar_speaker_board.h"
#include "sonar_clock.h"
#include "sonar_wav_player.h"
#include "sonar_waveform.h"
#include "FreeRTOS.h"
#include "task.h"
#include "xil_io.h"
#include <stddef.h>

static bool initialized, applied, prepared, running, completed, aborted, fault;
static uint32_t duration_us;
static uint64_t started_us;
static sonar_tx_mode_t mode=SONAR_TX_GENERATE;

/* Neither source reports completion, so playback ends a fixed time after the
 * trigger: the configured duration plus the FPGA start margin. */
static void poll_tx(void)
{
    if (!running) { return; }
    uint64_t elapsed=sonar_clock_us()-started_us;
    uint64_t end=(uint64_t)duration_us+SONAR_TX_START_MARGIN_US;
    if (elapsed>=end) {
        running = false;
        completed = true;
    }
}

bool sonar_chirp_init(void)
{
    if (initialized) { return !fault; }
    sonar_audio_profile_t p = sonar_platform_audio_profile();
    uint32_t required = SONAR_PROFILE_CHIRP_FREQ | SONAR_PROFILE_CHIRP_CONTROL;
    if ((sonar_audio_profile_check(&p) & required) != 0U ||
        !sonar_pl_gpio_init()) { return false; }

    Xil_Out32((UINTPTR)SONAR_CHIRP_FREQ_BASEADDR + 4U, 0U);
    Xil_Out32((UINTPTR)SONAR_CHIRP_FREQ_BASEADDR + 12U, 0U);
    Xil_Out32((UINTPTR)SONAR_CHIRP_CONTROL_BASEADDR + 4U, 0U);
    Xil_Out32((UINTPTR)SONAR_CHIRP_CONTROL_BASEADDR + 12U, 0U);
    SYNCHRONIZE_IO;
    if (!sonar_wav_player_init()) { return false; }
    initialized = true;
    return true;
}

uint32_t sonar_chirp_capabilities(void)
{
    if (!initialized || fault) { return 0U; }
    uint32_t caps=AUDIO_CAP_GENERATE;
    if (sonar_wav_player_available()) { caps|=AUDIO_CAP_WAV|AUDIO_CAP_WAV_GAIN; }
    return caps;
}

uint32_t sonar_chirp_status(void)
{
    poll_tx();
    return (applied ? AUDIO_CONFIG_VALID : 0U) |
           (prepared ? AUDIO_READY : 0U) |
           (running ? AUDIO_TX_BUSY : 0U) |
           (completed ? AUDIO_TX_DONE : 0U) |
           (aborted ? AUDIO_ABORT_DONE : 0U) |
           (fault ? AUDIO_CONFIG_ERROR : 0U);
}

bool sonar_chirp_apply(const sonar_experiment_config_t *c,
                       const sonar_chirp_values_t *v)
{
    sonar_chirp_values_t checked;
    poll_tx();
    if (!initialized || fault || running || prepared || c == NULL || v == NULL ||
        c->config_id == 0U || c->config_id > 65535U ||
        !sonar_experiment_calculate(c,sonar_waveform_samples(),&checked)) {
        return false;
    }
    if (c->mode==SONAR_TX_WAV && (!sonar_wav_player_available() ||
        sonar_waveform_uploading() || c->waveform_id!=sonar_waveform_id())) { return false; }
    if (v->start_phase_inc != checked.start_phase_inc ||
        v->phase_inc_step != checked.phase_inc_step ||
        v->total_samples != checked.total_samples ||
        v->amplitude != checked.amplitude || v->duration_us != checked.duration_us) {
        return false;
    }
    if (!sonar_pl_gpio_update(SONAR_PL_TRIGGER, 0U)) {
        fault = true;
        return false;
    }
    applied=false;
    if (c->mode==SONAR_TX_GENERATE) {
        if (!sonar_wav_player_select_generate()) { return false; }
        Xil_Out32((UINTPTR)SONAR_CHIRP_FREQ_BASEADDR, v->start_phase_inc);
        Xil_Out32((UINTPTR)SONAR_CHIRP_FREQ_BASEADDR + 8U, (uint32_t)v->phase_inc_step);
        Xil_Out32((UINTPTR)SONAR_CHIRP_CONTROL_BASEADDR, v->total_samples);
        Xil_Out32((UINTPTR)SONAR_CHIRP_CONTROL_BASEADDR + 8U, v->amplitude);
    } else if (!sonar_wav_player_configure(v->total_samples,c->amplitude_pct)) { return false; }
    SYNCHRONIZE_IO;
    mode=c->mode;
    duration_us = v->duration_us;
    applied = true;
    completed = aborted = false;
    return true;
}

bool sonar_chirp_prepare(void)
{
    poll_tx();
    if (!initialized || !applied || fault || running) { return false; }
    if (!sonar_pl_gpio_update(SONAR_PL_TRIGGER, 0U)) {
        fault = true;
        return false;
    }
    completed = aborted = false;
    prepared = true;
    return true;
}

void sonar_chirp_triggered(uint64_t trigger_us)
{
    if (!initialized || !prepared || running || fault) {
        fault = true;
        return;
    }
    started_us = trigger_us;
    prepared = false;
    running = true;
    completed = aborted = false;
}

bool sonar_chirp_done(void)
{
    poll_tx();
    return initialized && !fault && (completed || aborted);
}

bool sonar_chirp_error(void)
{ poll_tx(); return fault; }

bool sonar_chirp_abort(void)
{
    if (!initialized) { return true; }
    bool muted = sonar_speaker_board_enable(false);
    bool low = sonar_pl_gpio_update(SONAR_PL_TRIGGER, 0U);
    prepared = false;
    if (!muted || !low) { fault = true; return false; }

    /* There is no hardware abort: mute, then let the finite playback drain. */
    TickType_t start = xTaskGetTickCount();
    poll_tx();
    while (running) {
        if ((TickType_t)(xTaskGetTickCount() - start) >=
            pdMS_TO_TICKS(SONAR_AUDIO_IO_TIMEOUT_MS)) {
            fault = true;
            return false;
        }
        vTaskDelay(1U);
        poll_tx();
    }
    completed = false;
    aborted = true;
    return !fault;
}
