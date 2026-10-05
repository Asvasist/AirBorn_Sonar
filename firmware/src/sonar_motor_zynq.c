#include "sonar_motor_zynq.h"
#include "sonar_motor_config.h"
#include "sonar_ps_i2c.h"
#include "sonar_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "xil_io.h"
#include <stddef.h>

static sonar_motor_zynq_diagnostic_t diagnostic = {.init_reason="NOT_ATTEMPTED"};
const sonar_motor_zynq_diagnostic_t *sonar_motor_zynq_diagnostic(void)
{ return &diagnostic; }

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
static void retain_failure(bool reading, uint8_t address, uint8_t command, uint32_t length)
{
    if (diagnostic.transfer_failed) { return; }
    diagnostic.transfer_failed=true;
    diagnostic.failed_read=reading;
    diagnostic.failed_address=address;
    diagnostic.failed_command=command;
    diagnostic.failed_length=length;
    diagnostic.failed_irq=bus.last_irq;
    diagnostic.bus_status=read_register(NULL,PS_I2C_SR); /* After the driver's abort attempt. */
    diagnostic.quarantined=bus.quarantined;
}
static bool send(void *context, uint8_t address, const uint8_t *data, uint32_t length)
{
    bool ok=sonar_ps_i2c_write(context, address, data, length);
    if (!ok) { retain_failure(false,address,data!=NULL && length!=0U?data[0]:0U,length); }
    return ok;
}
static bool receive(void *context, uint8_t address, uint8_t *data, uint32_t length)
{
    bool ok=sonar_ps_i2c_read(context, address, data, length);
    if (!ok) { retain_failure(true,address,0U,length); }
    return ok;
}
static bool init_failed(const char *reason)
{ diagnostic.init_reason=reason; return false; }
#endif

bool sonar_motor_zynq_init(sonar_tic_io_t *io)
{
#if SONAR_MOTOR_ENABLE_HARDWARE
    uint32_t timeout, mio12, mio13;
    const sonar_ps_i2c_io_t registers = {NULL, read_register, write_register, ticks, wait_tick};
    if (initialized) { return init_failed("ALREADY_INITIALIZED"); }
    diagnostic=(sonar_motor_zynq_diagnostic_t){.init_reason="STARTING"};
    if (io == NULL || !sonar_ms_to_ticks(SONAR_MOTOR_I2C_TIMEOUT_MS,
        (uint32_t)configTICK_RATE_HZ, &timeout)) { return init_failed("INVALID_ARGUMENT_OR_TIMEOUT"); }
    diagnostic.aper=Xil_In32(SLCR_APER);
    diagnostic.reset=Xil_In32(SLCR_I2C_RESET);
    diagnostic.tri=Xil_In32(SLCR_MIO_TRI);
    diagnostic.mio12=Xil_In32(SLCR_MIO12);
    diagnostic.mio13=Xil_In32(SLCR_MIO13);
    diagnostic.loopback=Xil_In32(SLCR_LOOPBACK);
    /* Do not access a clock-gated controller or repurpose unrelated pin muxes. */
    if ((diagnostic.aper & (1U << 19U)) == 0U) { return init_failed("I2C1_CLOCK_DISABLED"); }
    if ((diagnostic.reset & 2U) != 0U) { return init_failed("I2C1_HELD_IN_RESET"); }
    if ((diagnostic.tri & ((1U << 12U) | (1U << 13U))) != 0U) { return init_failed("MIO12_13_TRISTATED"); }
    mio12=diagnostic.mio12; mio13=diagnostic.mio13;
    if (((mio12 & 0xfeU) != 0U && (mio12 & 0xfeU) != 0x40U) ||
        ((mio13 & 0xfeU) != 0U && (mio13 & 0xfeU) != 0x40U)) { return init_failed("MIO12_13_OTHER_PERIPHERAL"); }
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
    diagnostic.mio12=Xil_In32(SLCR_MIO12);
    diagnostic.mio13=Xil_In32(SLCR_MIO13);
    diagnostic.loopback=Xil_In32(SLCR_LOOPBACK);
    if ((diagnostic.mio12 & 0xfeU) != 0x40U ||
        (diagnostic.mio13 & 0xfeU) != 0x40U || (diagnostic.loopback & 8U) != 0U) { return init_failed("MIO_ROUTING_READBACK"); }
    diagnostic.bus_status=read_register(NULL,PS_I2C_SR);
    if (!sonar_ps_i2c_init(&bus, &registers, SONAR_MOTOR_I2C_INPUT_HZ, SONAR_MOTOR_I2C_HZ, timeout)) {
        return init_failed((diagnostic.bus_status & PS_I2C_BUSY)!=0U?"I2C1_BUS_BUSY":"I2C1_DRIVER_CONFIGURATION");
    }
    *io = (sonar_tic_io_t){&bus, send, receive, ticks};
    initialized = true;
    diagnostic.init_reason="OK";
    return true;
#else
    (void)io;
    diagnostic.init_reason="HARDWARE_DISABLED";
    return false;
#endif
}
