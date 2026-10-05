#include "sonar_socket.h"
#include "task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
/* The BSP's lwIP sockets write lwIP's global errno (LWIP_PROVIDE_ERRNO), not Newlib's. */
#include "lwip/errno.h"
#include <string.h>

#if !LWIP_SOCKET || NO_SYS
#error "Enable lwip220 SOCKET_API in the FreeRTOS domain and rebuild the platform."
#endif
#if LWIP_PROVIDE_ERRNO && defined(errno)
#error "Do not mix Newlib errno with the BSP's global lwIP socket errno."
#endif

bool sonar_socket_nonblocking(int fd)
{
    unsigned long enabled = 1U;
    return lwip_ioctl(fd, FIONBIO, &enabled) == 0;
}

bool sonar_socket_would_block(int error)
{
    return error == EAGAIN || error == EWOULDBLOCK || error == EINTR;
}

int sonar_socket_open(int type, uint16_t port)
{
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;

    int fd = lwip_socket(AF_INET, type, 0);
    if (fd < 0) { return -1; }
    if (!sonar_socket_nonblocking(fd) ||
        lwip_bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        (type == SOCK_STREAM && lwip_listen(fd, 1) < 0)) {
        lwip_close(fd);
        return -1;
    }
    return fd;
}

sonar_socket_status_t sonar_socket_transfer(int fd, uint8_t *data, uint32_t length, bool sending,
                                            TickType_t began, uint32_t timeout_ms,
                                            void (*idle)(void), uint32_t *done)
{
    uint32_t count = 0U;
    sonar_socket_status_t status = SONAR_SOCKET_DONE;
    while (count < length) {
        int n = sending ? lwip_send(fd, data + count, length - count, 0)
                        : lwip_recv(fd, data + count, length - count, 0);
        if (n == 0) { status = SONAR_SOCKET_CLOSED; break; }
        if (n < 0 && !sonar_socket_would_block(errno)) { status = SONAR_SOCKET_FAILED; break; }
        if (n > 0) { count += (uint32_t)n; }
        /* One deadline bounds the whole transfer, including a slow trickle. */
        if (count < length &&
            (TickType_t)(xTaskGetTickCount() - began) >= pdMS_TO_TICKS(timeout_ms)) {
            status = SONAR_SOCKET_TIMEOUT;
            break;
        }
        if (n < 0) {
            if (idle != NULL) { idle(); }
            vTaskDelay(1U);
        }
    }
    if (done != NULL) { *done = count; }
    return status;
}
