#include "sonar_tcp_console.h"
#include "sonar_console_buffer.h"
#include "sonar_stage2.h"
#include "sonar_console.h"
#include "sonar_socket.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/errno.h"
#include "xil_exception.h"
#include "xil_printf.h"
#include <string.h>

#define CONSOLE_PAYLOAD 512U
#define CONSOLE_HEARTBEAT_MS 3000U
#define CONSOLE_WRITE_MS 2000U
#define CONSOLE_HELLO_MS 5000U
static sonar_console_buffer_t history;
static bool started;

/* Cortex-A9 single-core PS application. Preserve the caller's IRQ mask;
 * this also works before the scheduler and never takes an RTOS mutex. */
static uint32_t lock_history(void)
{
    uint32_t saved = mfcpsr();
    Xil_ExceptionDisableMask(XIL_EXCEPTION_IRQ);
    __asm__ volatile ("" ::: "memory");
    return saved;
}
static void unlock_history(uint32_t saved)
{
    __asm__ volatile ("" ::: "memory");
    if ((saved & XIL_EXCEPTION_IRQ) == 0U) { Xil_ExceptionEnableMask(XIL_EXCEPTION_IRQ); }
}

/* --wrap=outbyte redirects xil_printf and the BSP's stdout/stderr writers.
 * Do not call sockets or RTOS services here. The original UART implementation
 * remains available via __real_outbyte for diagnostics. */
#if SONAR_CONSOLE_MIRROR_UART
extern void __real_outbyte(char c);
#endif
void __wrap_outbyte(char c)
{
    uint32_t saved = lock_history();
    sonar_console_buffer_put(&history, (uint8_t)c);
    unlock_history(saved);
#if SONAR_CONSOLE_MIRROR_UART
    __real_outbyte(c);
#endif
}

static uint64_t snapshot(void)
{
    uint32_t saved = lock_history();
    uint64_t end = history.complete;
    unlock_history(saved);
    return end;
}
static size_t read_history(uint64_t *cursor, uint64_t through, uint8_t *out, uint64_t *lost)
{
    uint32_t saved = lock_history();
    size_t count = sonar_console_buffer_read(&history, cursor, through, out, CONSOLE_PAYLOAD, lost);
    unlock_history(saved);
    return count;
}
static bool expired(TickType_t began, uint32_t milliseconds)
{ return (TickType_t)(xTaskGetTickCount() - began) >= pdMS_TO_TICKS(milliseconds); }
static bool handshake_io(int fd, uint8_t *p, uint32_t count, bool sending)
{
    return sonar_socket_transfer(fd, p, count, sending, xTaskGetTickCount(), CONSOLE_HELLO_MS,
                                 NULL, NULL) == SONAR_SOCKET_DONE;
}
static void note(const char *text)
{ sonar_console_lock(); xil_printf("CONSOLE: %s\r\n", text); sonar_console_unlock(); }

static void serve(int fd)
{
    uint8_t hello[11];
    uint8_t reply[] = "CONSOLE1\r\n";
    if (!sonar_socket_nonblocking(fd) || !handshake_io(fd, hello, sizeof(hello), false) ||
        memcmp(hello, "SONARCON1\r\n", sizeof(hello)) != 0 ||
        !handshake_io(fd, reply, sizeof(reply) - 1U, true)) { return; }

    uint64_t cursor = 0U, history_end = snapshot();
    bool live = false;
    uint8_t packet[4U + CONSOLE_PAYLOAD];
    size_t size = 0U, sent = 0U;
    TickType_t last_rx = xTaskGetTickCount(), write_began = last_rx;
    for (;;) {
        /* Commands are serviced even while console output is backpressured. */
        uint8_t input[64];
        int n = lwip_recv(fd, input, sizeof(input), 0);
        int error = n < 0 ? errno : 0;
        if (n == 0 || (n < 0 && !sonar_socket_would_block(error))) { break; }
        if (n > 0) {
            last_rx = xTaskGetTickCount();
            bool stop = memchr(input, 'x', (size_t)n) != NULL || memchr(input, 'X', (size_t)n) != NULL;
            if (stop) { sonar_stage2_key('x'); }
            else {
                for (int i = 0; i < n; ++i) {
                    if (input[i] == 0U) { continue; } /* Heartbeat. */
                    if (!live || input[i] > 127U) { goto disconnected; }
                    sonar_stage2_key(input[i]);
                }
            }
        }
        if (expired(last_rx, CONSOLE_HEARTBEAT_MS)) { break; }

        if (sent == size) {
            uint8_t type = live ? 'L' : 'H';
            uint64_t lost;
            size_t count = read_history(&cursor, live ? UINT64_MAX : history_end, packet + 4U, &lost);
            if (lost != 0U) {
                type = 'G'; count = 8U;
                for (unsigned i = 0U; i < 8U; ++i) { packet[4U + i] = (uint8_t)(lost >> (8U * i)); }
            } else if (!live && count == 0U && cursor >= history_end) {
                type = 'R'; live = true; /* Live boundary: GUI now requests fresh status. */
            } else if (count == 0U) { vTaskDelay(1U); continue; }
            packet[0] = 'C'; packet[1] = type;
            packet[2] = (uint8_t)count; packet[3] = (uint8_t)(count >> 8U);
            size = count + 4U; sent = 0U; write_began = xTaskGetTickCount();
        }
        n = lwip_send(fd, packet + sent, size - sent, 0);
        error = n < 0 ? errno : 0;
        if (n > 0) { sent += (size_t)n; }
        else if (n == 0 || !sonar_socket_would_block(error)) { break; }
        if (expired(write_began, CONSOLE_WRITE_MS)) { break; }
        vTaskDelay(1U);
    }
disconnected:
    /* Priority Stop clears queued start/settings in the experiment task. */
    sonar_stage2_key('x');
    note("connection lost/closed; Stop requested; queued recordings retained.");
}

static void server(void *unused)
{
    (void)unused;
    int listener = sonar_socket_open(SOCK_STREAM, SONAR_CONSOLE_PORT);
    if (listener < 0) { note("TCP 5004 bind failed."); vTaskDelete(NULL); return; }
    note("TCP 5004 ready; GENERATE controls and status now use Ethernet.");
    for (;;) {
        int fd = lwip_accept(listener, NULL, NULL);
        if (fd < 0) { vTaskDelay(1U); continue; }
        serve(fd);
        lwip_close(fd);
    }
}
bool sonar_tcp_console_start(void)
{
    if (started) { return false; }
    started = xTaskCreate(server, "console", 2048U, NULL, 2U, NULL) == pdPASS;
    return started;
}
