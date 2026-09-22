#include "sonar_speaker_board.h"
#include "sonar_codec_bus.h"
#include "sonar_platform.h"
#include "sonar_pl_gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "xil_io.h"
#include <stddef.h>

static sonar_codec_bus_t bus;
static sonar_speaker_t speaker;
static bool initialized;
static uint32_t read_iic(void *context, uint32_t offset)
{ (void)context; return Xil_In32((UINTPTR)(SONAR_EXPECTED_IIC_BASE + offset)); }
static void write_iic(void *context, uint32_t offset, uint32_t value)
{
    (void)context;
    Xil_Out32((UINTPTR)(SONAR_EXPECTED_IIC_BASE + offset), value);
    __asm__ volatile ("dsb sy" ::: "memory");
}
static bool enable_output(void *context, bool enabled)
{
    (void)context;
    return sonar_pl_gpio_update(SONAR_PL_CODEC_READY, enabled ? SONAR_PL_CODEC_READY : 0U);
}
static bool write_codec(void *context, uint8_t address, const uint8_t bytes[2], uint32_t token)
{ return sonar_codec_bus_write(context, address, bytes, token); }
static bool abort_codec(void *context) { return sonar_codec_bus_abort(context); }
bool sonar_speaker_board_init(void)
{
    sonar_profile_t profile;
    sonar_speaker_config_t config;
    const sonar_codec_bus_io_t io = {NULL, read_iic, write_iic};
    const sonar_speaker_io_t codec_io = {&bus, write_codec, abort_codec, enable_output};
    if (!SONAR_SPEAKER_ENABLE_HARDWARE || initialized) { return false; }
    profile = sonar_platform_profile();
    if (sonar_profile_check(&profile) != 0U ||
        !sonar_speaker_default_config((uint32_t)configTICK_RATE_HZ, &config)) { return false; }
    if (!sonar_pl_gpio_init() || !enable_output(NULL, false) || !sonar_codec_bus_init(&bus, &io) ||
        !sonar_speaker_init(&speaker, &config, &codec_io)) { return false; }
    initialized = true;
    return true;
}
bool sonar_speaker_board_begin(uint32_t now)
{ return initialized && sonar_speaker_begin(&speaker, now); }
void sonar_speaker_board_poll(uint32_t now)
{
    sonar_codec_event_t event;
    if (!initialized) { return; }
    sonar_speaker_poll(&speaker, now);
    if (sonar_codec_bus_event(&bus, &event)) {
        sonar_speaker_event(&speaker, event.token, event.acknowledged, now);
    }
}
bool sonar_speaker_board_enable(bool enabled)
{ return initialized && sonar_speaker_enable(&speaker, enabled); }
void sonar_speaker_board_cancel(void)
{ if (initialized) { sonar_speaker_cancel(&speaker); } }
const sonar_speaker_t *sonar_speaker_board_state(void)
{ return initialized ? &speaker : NULL; }

bool sonar_speaker_board_output_ready(void)
{
    bool ready;
    taskENTER_CRITICAL();
    ready = initialized && speaker.state == SPEAKER_READY && speaker.output_enabled;
    taskEXIT_CRITICAL();
    return ready;
}
