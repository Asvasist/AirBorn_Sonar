#include "sonar_motor_zynq.h"
#include "sonar_motor_config.h"
#include "sonar_ps_i2c.h"
#include "sonar_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "xil_io.h"
#include <stddef.h>

#if SONAR_MOTOR_ENABLE_HARDWARE
#define I2C1_BASE UINT32_C(0xe0005000)
#define SLCR_UNLOCK UINT32_C(0xf8000008)
#define SLCR_LOCK UINT32_C(0xf8000004)
#define SLCR_APER UINT32_C(0xf800012c)
#define SLCR_I2C_RESET UINT32_C(0xf8000224)
#define SLCR_MIO12 UINT32_C(0xf8000730)
#define SLCR_MIO13 UINT32_C(0xf8000734)
#define SLCR_LOOPBACK UINT32_C(0xf8000804)
#define SLCR_MIO_TRI UINT32_C(0xf800080c)
static sonar_ps_i2c_t bus;
static bool initialized;

static uint32_t read_register(void *context, uint32_t offset)
{ (void)context; return Xil_In32((UINTPTR)(I2C1_BASE + offset)); }
static void write_register(void *context, uint32_t offset, uint32_t value)
{
    (void)context;
    Xil_Out32((UINTPTR)(I2C1_BASE + offset), value);
    __asm__ volatile ("dsb sy" ::: "memory");
}
static uint32_t ticks(void *context) { (void)context; return (uint32_t)xTaskGetTickCount(); }
static void wait_tick(void *context) { (void)context; vTaskDelay(1U); }
static bool send(void *context, uint8_t address, const uint8_t *data, uint32_t length)
{ return sonar_ps_i2c_write(context, address, data, length); }
static bool receive(void *context, uint8_t address, uint8_t *data, uint32_t length)
{ return sonar_ps_i2c_read(context, address, data, length); }
#endif

bool sonar_motor_zynq_init(sonar_tic_io_t *io)
{
#if SONAR_MOTOR_ENABLE_HARDWARE
    uint32_t timeout, mio12, mio13;
    const sonar_ps_i2c_io_t registers = {NULL, read_register, write_register, ticks, wait_tick};
    if (io == NULL || initialized || !sonar_ms_to_ticks(SONAR_MOTOR_I2C_TIMEOUT_MS,
        (uint32_t)configTICK_RATE_HZ, &timeout)) { return false; }
    /* Do not access a clock-gated controller or repurpose unrelated pin muxes. */
    if ((Xil_In32(SLCR_APER) & (1U << 19U)) == 0U ||
        (Xil_In32(SLCR_I2C_RESET) & 2U) != 0U ||
        (Xil_In32(SLCR_MIO_TRI) & ((1U << 12U) | (1U << 13U))) != 0U) { return false; }
    mio12 = Xil_In32(SLCR_MIO12); mio13 = Xil_In32(SLCR_MIO13);
    if (((mio12 & 0xfeU) != 0U && (mio12 & 0xfeU) != 0x40U) ||
        ((mio13 & 0xfeU) != 0U && (mio13 & 0xfeU) != 0x40U)) { return false; }
    /* The supplied test used this PS routing fix. Preserve electrical settings,
     * update only the peripheral selection and internal-loopback bit. */
    taskENTER_CRITICAL();
    Xil_Out32(SLCR_UNLOCK, 0xdf0dU);
    Xil_Out32(SLCR_MIO12, (mio12 & ~0xe0U) | 0x40U);
    Xil_Out32(SLCR_MIO13, (mio13 & ~0xe0U) | 0x40U);
    Xil_Out32(SLCR_LOOPBACK, Xil_In32(SLCR_LOOPBACK) & ~8U);
    Xil_Out32(SLCR_LOCK, 0x767bU);
    __asm__ volatile ("dsb sy" ::: "memory");
    taskEXIT_CRITICAL();
    if ((Xil_In32(SLCR_MIO12) & 0xfeU) != 0x40U ||
        (Xil_In32(SLCR_MIO13) & 0xfeU) != 0x40U || (Xil_In32(SLCR_LOOPBACK) & 8U) != 0U) { return false; }
    if (!sonar_ps_i2c_init(&bus, &registers, SONAR_MOTOR_I2C_INPUT_HZ, SONAR_MOTOR_I2C_HZ, timeout)) { return false; }
    *io = (sonar_tic_io_t){&bus, send, receive, ticks};
    initialized = true;
    return true;
#else
    (void)io;
    return false;
#endif
}
