#include "sonar_scan_task.h"
#include "sonar_scan_command.h"
#include "sonar_scan_sim.h"
#include "sonar_console.h"
#include "FreeRTOS.h"
#include "task.h"
#include "xil_printf.h"
#include "xuartps_hw.h"
#include "bspconfig.h"
#include <stddef.h>
#define SCAN_STACK_WORDS 1536U
static TaskHandle_t handle;
static bool inhibited;
static sonar_scan_sim_t simulation;
static sonar_scan_config_t plan;
#if configSUPPORT_STATIC_ALLOCATION == 1
static StaticTask_t storage;
static StackType_t stack[SCAN_STACK_WORDS];
#endif
static void show(void)
{
    xil_printf("mode=%s shots=%u capture_bytes=%u speed_units=%d (10000 units = 1 microstep/s)\r\n",
        plan.mode == SCAN_CONTINUOUS ? "continuous" : "step", (unsigned int)plan.shots,
        (unsigned int)plan.capture_bytes, (int)plan.velocity);
    xil_printf("steps_per_rev=%u reference=%d confirmed=%u start_mdeg=%d step_mdeg=%d\r\n",
        (unsigned int)plan.steps_per_revolution, (int)plan.reference_steps,
        (unsigned int)plan.reference_confirmed, (int)plan.start_angle_mdeg, (int)plan.step_angle_mdeg);
    xil_printf("interval_ms=%u settle_ms=%u motion_timeout_ms=%u capture_timeout_ms=%u\r\n",
        (unsigned int)plan.interval_ms, (unsigned int)plan.settle_ms,
        (unsigned int)plan.motion_timeout_ms, (unsigned int)plan.capture_timeout_ms);
    xil_printf("stop_timeout_ms=%u frame_hold_timeout_ms=%u\r\n",
        (unsigned int)plan.stop_timeout_ms, (unsigned int)plan.frame_hold_timeout_ms);
}
static void help(void)
{
    xil_printf("SCAN: show | check | simulate | run | cancel | help\r\n");
    xil_printf("mode step|continuous; reference <commanded_steps>; unreference\r\n");
    xil_printf("set <field> <value>; fields: shots capture_bytes steps_per_rev start_deg step_deg speed\r\n");
    xil_printf("interval_ms settle_ms motion_timeout_ms capture_timeout_ms stop_timeout_ms frame_hold_timeout_ms\r\n");
    xil_printf("speed is signed microsteps/s; angles use degrees. Enter submits a command.\r\n");
}
static void command(const char *line, bool *running, bool blocked)
{
    sonar_scan_config_t edited = plan;
    sonar_scan_command_t action = sonar_scan_command(&edited, line);
    if (action == SCAN_CMD_CANCEL) { if (*running) { sonar_scan_cancel(&simulation.scan); } return; }
    if (action == SCAN_CMD_SHOW) { show(); return; }
    if (action == SCAN_CMD_HELP) { help(); return; }
    if (action == SCAN_CMD_INVALID) { xil_printf("Invalid command; type help. Plan unchanged.\r\n"); return; }
    if (*running) { xil_printf("Simulation active; cancel or wait before editing or starting another run.\r\n"); return; }
    if (action == SCAN_CMD_UPDATED) { plan = edited; xil_printf("Plan updated; check validates the complete plan.\r\n"); return; }
    sonar_scan_timing_t timing;
    sonar_scan_config_error_t valid = sonar_scan_config_validate(&plan, 1000U, &timing);
    xil_printf("PLAN %s (simulation clock 1000 Hz)\r\n", sonar_scan_config_error_name(valid));
    if (action == SCAN_CMD_RUN) {
        xil_printf("LIVE SCAN BLOCKED: matching FPGA export, CPU TX/RX status, overflow and stop acknowledgement required.\r\n");
        xil_printf("No hardware commands sent. See docs/FPGA_REQUESTS.md and docs/INTEGRATION.md.\r\n");
    } else if (action == SCAN_CMD_SIMULATE && valid == SCAN_CONFIG_OK) {
        if (blocked) { xil_printf("Simulation inhibited by latched system-health fault; reset first.\r\n"); return; }
        *running = sonar_scan_sim_init(&simulation, &plan);
        xil_printf("SIMULATION: virtual time, synthetic completions, no audio or motor operation.\r\n");
    }
}
static void scan_task(void *argument)
{
    char line[128]; size_t length = 0U;
    bool discarded = false, running = false, blocked;
    (void)argument;
    plan = sonar_scan_config_default();
    sonar_console_lock(); help(); show(); sonar_console_unlock();
    for (;;) {
        taskENTER_CRITICAL(); blocked = inhibited; taskEXIT_CRITICAL();
        if (blocked && running) { sonar_scan_cancel(&simulation.scan); }
        /* Bound RX work so pasted commands cannot starve the scheduler. */
        for (unsigned i = 0U; i < 32U && XUartPs_IsReceiveData(STDIN_BASEADDRESS); ++i) {
            uint8_t c = XUartPs_RecvByte(STDIN_BASEADDRESS);
            if (c == '\r' || c == '\n') {
                sonar_console_lock();
                if (discarded) { xil_printf("Command discarded: too long or contains control characters.\r\n"); }
                else if (length != 0U) { line[length] = '\0'; command(line, &running, blocked); }
                sonar_console_unlock(); length = 0U; discarded = false;
            } else if (c == 8U || c == 127U) { if (length != 0U) { --length; } }
            else if (c >= 32U && c <= 126U) {
                if (length < sizeof(line) - 1U) { line[length++] = (char)c; } else { discarded = true; }
            } else { discarded = true; }
        }
        if (running) {
            sonar_scan_sim_step(&simulation);
            sonar_measurement_t frame;
            if (sonar_scan_frame(&simulation.scan, &frame)) {
                if (frame.index < 5U || frame.index + 1U == plan.shots) {
                    sonar_console_lock();
                    xil_printf("SIM frame=%u virtual_trigger_ms=%u bytes=%u requested_mdeg=%d angle_valid=%u\r\n",
                        (unsigned int)(frame.index + 1U), (unsigned int)frame.trigger_tick,
                        (unsigned int)frame.bytes, (int)frame.requested_angle_mdeg,
                        (unsigned int)frame.requested_angle_valid);
                    sonar_console_unlock();
                }
                (void)sonar_scan_sim_release(&simulation);
            }
            if (simulation.scan.state == SCAN_COMPLETE || simulation.scan.state == SCAN_FAULT) {
                running = false; sonar_console_lock();
                xil_printf("SIM %s frames=%u fault=%u virtual_ms=%u\r\n",
                    sonar_scan_state_name(simulation.scan.state), (unsigned int)simulation.frames,
                    (unsigned int)simulation.scan.fault, (unsigned int)simulation.now);
                sonar_console_unlock();
            }
        }
        vTaskDelay(1U);
    }
}
void sonar_scan_task_inhibit(void)
{ taskENTER_CRITICAL(); inhibited = true; taskEXIT_CRITICAL(); }
bool sonar_scan_task_create(void)
{
    if (handle != NULL) { return false; }
#if configSUPPORT_STATIC_ALLOCATION == 1
    handle = xTaskCreateStatic(scan_task, "scan", SCAN_STACK_WORDS, NULL,
        tskIDLE_PRIORITY + 2U, stack, &storage);
    return handle != NULL;
#else
    return xTaskCreate(scan_task, "scan", SCAN_STACK_WORDS, NULL,
        tskIDLE_PRIORITY + 2U, &handle) == pdPASS;
#endif
}
