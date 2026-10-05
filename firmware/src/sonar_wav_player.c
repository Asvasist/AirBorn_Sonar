#include "sonar_wav_player.h"
#include "sonar_wav_board_config.h"
#include "sonar_tx_bram.h"
#include "sonar_audio_hw.h"
#include "sonar_profile.h"
#include "FreeRTOS.h"
#include "task.h"
#include "xil_io.h"
#include <stddef.h>

static bool available;

static bool address(uint32_t a)
{
    uint64_t bram_base=SONAR_WAV_BRAM_BASE;
    uint64_t bram_end=bram_base+SONAR_WAV_BRAM_BYTES;
    return a >= 0x40000000U && a <= 0xbffffffcU && (a & 3U) == 0U &&
        (a & 0xffff0000U) != SONAR_EXPECTED_GPIO_BASE &&
        (a & 0xffff0000U) != SONAR_EXPECTED_DMA_BASE &&
        (a & 0xffff0000U) != SONAR_EXPECTED_IIC_BASE &&
        (a & 0xffff0000U) != 0x41220000U &&
        !((uint64_t)a >= bram_base && (uint64_t)a < bram_end);
}

static bool mapping_valid(void)
{
    const uint32_t regs[] = {SONAR_WAV_MODE_REG, SONAR_WAV_MODE_TRI,
        SONAR_WAV_COUNT_REG, SONAR_WAV_COUNT_TRI, SONAR_WAV_GAIN_REG,
        SONAR_WAV_GAIN_TRI, SONAR_WAV_COMMAND_REG, SONAR_WAV_COMMAND_TRI,
        SONAR_WAV_STATUS_REG};
    if (!SONAR_WAV_ENABLED || !SONAR_WAV_MODE_REG || !SONAR_WAV_COUNT_REG ||
        SONAR_WAV_GENERATE_VALUE == SONAR_WAV_PLAYBACK_VALUE ||
        SONAR_WAV_COUNT_MINUS_ONE > 1U ||
        (SONAR_WAV_GAIN_REG && !SONAR_WAV_GAIN_FULL_SCALE) ||
        (!SONAR_WAV_GAIN_REG && SONAR_WAV_GAIN_TRI) ||
        (!SONAR_WAV_COMMAND_REG && (SONAR_WAV_COMMAND_TRI ||
            SONAR_WAV_REWIND_VALUE || SONAR_WAV_ABORT_VALUE)) ||
        (!SONAR_WAV_STATUS_REG && (SONAR_WAV_READY_MASK | SONAR_WAV_BUSY_MASK |
            SONAR_WAV_DONE_MASK | SONAR_WAV_ERROR_MASK)) ||
        (SONAR_WAV_DONE_MASK & (SONAR_WAV_BUSY_MASK | SONAR_WAV_ERROR_MASK)) ||
        (SONAR_WAV_REWIND_VALUE && SONAR_WAV_REWIND_VALUE == SONAR_WAV_COMMAND_IDLE) ||
        (SONAR_WAV_ABORT_VALUE && SONAR_WAV_ABORT_VALUE == SONAR_WAV_COMMAND_IDLE)) {
        return false;
    }
    for (unsigned i = 0; i < sizeof(regs) / sizeof(regs[0]); ++i) {
        if (!regs[i]) { continue; }
        if (!address(regs[i])) { return false; }
        if ((regs[i]&0xffff0000U)==0x41210000U &&
            !((i==2U && regs[i]==0x41210000U) || (i==3U && regs[i]==0x41210004U) ||
              (i==4U && regs[i]==0x41210008U) || (i==5U && regs[i]==0x4121000cU))) {
            return false;
        }
        for (unsigned j = 0; j < i; ++j) {
            if (regs[j] == regs[i]) { return false; }
        }
    }
    return true;
}

static void write_reg(uint32_t a, uint32_t value)
{ Xil_Out32((UINTPTR)a, value); SYNCHRONIZE_IO; }

static uint32_t status(void)
{ return available && SONAR_WAV_STATUS_REG ? Xil_In32((UINTPTR)SONAR_WAV_STATUS_REG) : 0U; }

static void pulse(uint32_t value)
{
    if (SONAR_WAV_COMMAND_REG && value) {
        write_reg(SONAR_WAV_COMMAND_REG, value);
        vTaskDelay(1U);
        write_reg(SONAR_WAV_COMMAND_REG, SONAR_WAV_COMMAND_IDLE);
    }
}

bool sonar_wav_player_init(void)
{
    if (available) { return true; }
    if (!mapping_valid() || !sonar_tx_bram_init()) { return false; }
    /* Load safe values before enabling optional AXI GPIO output directions. */
    write_reg(SONAR_WAV_MODE_REG, SONAR_WAV_GENERATE_VALUE);
    if (SONAR_WAV_COMMAND_REG) { write_reg(SONAR_WAV_COMMAND_REG, SONAR_WAV_COMMAND_IDLE); }
    if (SONAR_WAV_GAIN_REG) { write_reg(SONAR_WAV_GAIN_REG, 0U); }
    const uint32_t dirs[] = {SONAR_WAV_MODE_TRI, SONAR_WAV_COUNT_TRI,
        SONAR_WAV_GAIN_TRI, SONAR_WAV_COMMAND_TRI};
    for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
        if (dirs[i]) { write_reg(dirs[i], 0U); }
    }
    available = true;
    return true;
}

bool sonar_wav_player_available(void) { return available; }
bool sonar_wav_player_has_gain(void) { return available && (SONAR_WAV_GAIN_REG != 0U || SONAR_WAV_SAMPLE_STRIDE==4U); }
bool sonar_wav_player_has_done(void) { return available && SONAR_WAV_DONE_MASK != 0U; }
bool sonar_wav_player_error(void) { return (status() & SONAR_WAV_ERROR_MASK) != 0U; }
bool sonar_wav_player_busy(void) { return (status() & SONAR_WAV_BUSY_MASK) != 0U; }
bool sonar_wav_player_done(void)
{
    uint32_t s = status();
    return sonar_wav_player_has_done() && (s & SONAR_WAV_DONE_MASK) != 0U &&
        (s & (SONAR_WAV_BUSY_MASK | SONAR_WAV_ERROR_MASK)) == 0U;
}

bool sonar_wav_player_select_generate(void)
{
    if (!available) { return !SONAR_WAV_ENABLED; }
    if (status() & SONAR_WAV_BUSY_MASK) { return false; }
    write_reg(SONAR_WAV_MODE_REG, SONAR_WAV_GENERATE_VALUE);
    return true;
}

bool sonar_wav_player_configure(uint32_t samples, uint32_t amplitude_pct)
{
    if (!available || !samples || samples > sonar_tx_bram_capacity() ||
        amplitude_pct > 100U || (!sonar_wav_player_has_gain() && amplitude_pct != 100U) ||
        (status() & SONAR_WAV_BUSY_MASK)) { return false; }
    if (!SONAR_WAV_GAIN_REG && SONAR_WAV_SAMPLE_STRIDE==4U &&
        !sonar_tx_bram_set_gain(samples,amplitude_pct)) { return false; }
    write_reg(SONAR_WAV_COUNT_REG, samples - SONAR_WAV_COUNT_MINUS_ONE);
    if (SONAR_WAV_GAIN_REG) {
        write_reg(SONAR_WAV_GAIN_REG, (uint32_t)(((uint64_t)amplitude_pct *
            SONAR_WAV_GAIN_FULL_SCALE + 50U) / 100U));
    }
    write_reg(SONAR_WAV_MODE_REG, SONAR_WAV_PLAYBACK_VALUE);
    return true;
}

bool sonar_wav_player_prepare(void)
{
    if (!available || (status() & SONAR_WAV_BUSY_MASK)) { return false; }
    sonar_tx_bram_fence();
    pulse(SONAR_WAV_REWIND_VALUE);
    TickType_t start = xTaskGetTickCount();
    for (;;) {
        uint32_t s = status();
        if (s & SONAR_WAV_ERROR_MASK) { return false; }
        if (!(s & (SONAR_WAV_BUSY_MASK | SONAR_WAV_DONE_MASK)) &&
            (!SONAR_WAV_READY_MASK || (s & SONAR_WAV_READY_MASK) == SONAR_WAV_READY_MASK)) {
            return true;
        }
        if ((TickType_t)(xTaskGetTickCount() - start) >= pdMS_TO_TICKS(SONAR_AUDIO_IO_TIMEOUT_MS)) {
            return false;
        }
        vTaskDelay(1U);
    }
}

bool sonar_wav_player_abort(void)
{
    /* With no observable BUSY bit, keep the finite-playback drain in the caller. */
    if (!available || !SONAR_WAV_ABORT_VALUE || !SONAR_WAV_BUSY_MASK) { return false; }
    pulse(SONAR_WAV_ABORT_VALUE);
    TickType_t start = xTaskGetTickCount();
    while (status() & SONAR_WAV_BUSY_MASK) {
        if ((TickType_t)(xTaskGetTickCount() - start) >= pdMS_TO_TICKS(SONAR_AUDIO_IO_TIMEOUT_MS)) {
            return false;
        }
        vTaskDelay(1U);
    }
    return !sonar_wav_player_error();
}
