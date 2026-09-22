#include "sonar_mic_zynq.h"
#include "sonar_mic_config.h"
#include "sonar_config.h"
#include "sonar_platform.h"
#include "sonar_pl_gpio.h"
#include "xaxidma.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "xinterrupt_wrap.h"
#include "xparameters.h"
#include "xstatus.h"
#include <stddef.h>

#ifndef SDT
#error "This adapter targets the Vitis 2025.2 SDT FreeRTOS domain."
#endif
#if configUSE_TASK_NOTIFICATIONS != 1
#error "Enable FreeRTOS task notifications in the Vitis BSP."
#endif

static struct {
    XAxiDma dma;
    TaskHandle_t owner;
    volatile sonar_mic_event_t event;
    volatile uint32_t generation;
    volatile bool pending;
    uint16_t irq;
    bool initialized;
} hardware;

static void barrier(void) { __asm__ volatile ("dsb sy" ::: "memory"); }

static void dma_interrupt(void *reference)
{
    XAxiDma *dma = reference;
    BaseType_t wake = pdFALSE;
    uint32_t irq = XAxiDma_IntrGetIrq(dma, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_IntrAckIrq(dma, irq, XAXIDMA_DEVICE_TO_DMA);
    if ((irq & (XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_ERROR_MASK)) == 0U) { return; }

    /* One outstanding transfer, one terminal event. No reset, cache work or logging here. */
    XAxiDma_IntrDisable(dma, (u32)XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    hardware.event.generation = hardware.generation;
    hardware.event.flags = (irq & XAXIDMA_IRQ_ERROR_MASK) != 0U ? SONAR_MIC_IRQ_ERROR : SONAR_MIC_IRQ_DONE;
    hardware.event.bytes = XAxiDma_ReadReg(dma->RegBase + XAXIDMA_RX_OFFSET,
                                          XAXIDMA_BUFFLEN_OFFSET);
    hardware.event.tick = (uint32_t)xTaskGetTickCountFromISR();
    /* This export does not expose the PDM packer's sticky overflow to the PS. */
    hardware.event.overflow_observable = false;
    hardware.pending = true;
    vTaskNotifyGiveFromISR(hardware.owner, &wake);
    portYIELD_FROM_ISR(wake);
}

static bool prepare_buffer(void *context, uint8_t *buffer, uint32_t span)
{
    uint32_t status;
    (void)context;
    vPortDisableInterrupt(hardware.irq);
    XAxiDma_IntrDisable(&hardware.dma, (u32)XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    status = XAxiDma_ReadReg(hardware.dma.RegBase + XAXIDMA_RX_OFFSET, XAXIDMA_SR_OFFSET);
    if ((status & XAXIDMA_ERR_ALL_MASK) != 0U ||
        (status & (XAXIDMA_HALTED_MASK | XAXIDMA_IDLE_MASK)) == 0U) { return false; }
    if (!sonar_pl_gpio_update(SONAR_PL_TRIGGER, 0U)) { return false; }
    barrier();
    XAxiDma_IntrAckIrq(&hardware.dma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    taskENTER_CRITICAL();
    hardware.pending = false;
    taskEXIT_CRITICAL();
    (void)ulTaskNotifyTake(pdTRUE, 0U);
    /* Isolated, aligned cache lines ensure invalidation never discards other data. */
    Xil_DCacheFlushRange((INTPTR)buffer, span);
    barrier();
    return true;
}

static bool arm_dma(void *context, uint8_t *buffer, uint32_t bytes, uint32_t generation)
{
    u32 status;
    (void)context;
    hardware.generation = generation;
    status = XAxiDma_SimpleTransfer(&hardware.dma, (UINTPTR)buffer, bytes,
                                    XAXIDMA_DEVICE_TO_DMA);
    if (status != XST_SUCCESS) { return false; }
    XAxiDma_IntrEnable(&hardware.dma, (XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_ERROR_MASK),
                       XAXIDMA_DEVICE_TO_DMA);
    vPortEnableInterrupt(hardware.irq);
    barrier();
    return true;
}

static bool trigger_capture(void *context)
{
    (void)context;
    if (!sonar_pl_gpio_update(SONAR_PL_TRIGGER, SONAR_PL_TRIGGER)) { return false; }
    barrier();
    /* The exported GPIO starts RX and TX together. This is a request, not a sample clock. */
    vTaskDelay(1U);
    if (!sonar_pl_gpio_update(SONAR_PL_TRIGGER, 0U)) { return false; }
    barrier();
    return true;
}

static bool finish_buffer(void *context, uint8_t *buffer, uint32_t span)
{
    uint32_t status;
    (void)context;
    status = XAxiDma_ReadReg(hardware.dma.RegBase + XAXIDMA_RX_OFFSET, XAXIDMA_SR_OFFSET);
    if ((status & XAXIDMA_IDLE_MASK) == 0U || (status & XAXIDMA_ERR_ALL_MASK) != 0U) {
        return false;
    }
    barrier();
    Xil_DCacheInvalidateRange((INTPTR)buffer, span);
    barrier();
    return true;
}

static bool abort_dma(void *context)
{
    TickType_t start = xTaskGetTickCount();
    uint32_t timeout;
    (void)context;
    bool trigger_cleared = sonar_pl_gpio_update(SONAR_PL_TRIGGER, 0U);
    vPortDisableInterrupt(hardware.irq);
    XAxiDma_IntrDisable(&hardware.dma, (u32)XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    if (!sonar_ms_to_ticks(SONAR_MIC_RESET_TIMEOUT_MS, (uint32_t)configTICK_RATE_HZ, &timeout)) {
        return false;
    }
    XAxiDma_Reset(&hardware.dma);
    while (XAxiDma_ResetIsDone(&hardware.dma) == 0) {
        if ((uint32_t)(xTaskGetTickCount() - start) >= timeout) { return false; }
        vTaskDelay(1U);
    }
    barrier();
    return trigger_cleared;
}

bool sonar_mic_zynq_init(TaskHandle_t owner, sonar_mic_io_t *io)
{
    XAxiDma_Config *config;
    sonar_profile_t profile = sonar_platform_profile();
    if (hardware.initialized || owner == NULL || io == NULL || sonar_profile_check(&profile) != 0U) {
        return false;
    }
    config = XAxiDma_LookupConfig((UINTPTR)SONAR_EXPECTED_DMA_BASE);
    if (config == NULL || config->HasMm2S != 0 || config->HasS2Mm != 1 ||
        config->HasSg != 0 || config->MicroDmaMode != 0 || config->AddrWidth != 32 ||
        config->S2MmDataWidth != 32 || config->HasS2MmDRE != 0 ||
        config->S2MmNumChannels != 1 || config->SgLengthWidth != 26 ||
        config->IntrId[0] != 0x401dU || config->IntrParent != (UINTPTR)0xf8f01000U) {
        return false;
    }
    /* S2MM is the only exported IRQ: it occupies IntrId[0], not IntrId[1]. */
    hardware.irq = config->IntrId[0];
    hardware.owner = owner;
    if (!sonar_pl_gpio_init()) { return false; }
    if (!sonar_pl_gpio_update(SONAR_PL_TRIGGER, 0U)) { return false; }
    barrier();
    if (XAxiDma_CfgInitialize(&hardware.dma, config) != XST_SUCCESS) { return false; }
    XAxiDma_IntrDisable(&hardware.dma, (u32)XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    if (xPortInstallInterruptHandler(hardware.irq, dma_interrupt, &hardware.dma) != pdPASS) {
        return false;
    }
    /* SDT wrapper decodes 0x401d; do not feed its SPI index to a raw GIC API. */
    XSetPriorityTriggerType(hardware.irq,
        (u8)(configMAX_API_CALL_INTERRUPT_PRIORITY << portPRIORITY_SHIFT), config->IntrParent);
    vPortDisableInterrupt(hardware.irq);
    hardware.initialized = true;
    *io = (sonar_mic_io_t){NULL, prepare_buffer, arm_dma, trigger_capture, finish_buffer, abort_dma};
    return true;
}

bool sonar_mic_zynq_take_event(sonar_mic_event_t *event)
{
    bool pending;
    if (event == NULL) { return false; }
    taskENTER_CRITICAL();
    pending = hardware.pending;
    if (pending) { *event = hardware.event; hardware.pending = false; }
    taskEXIT_CRITICAL();
    return pending;
}
