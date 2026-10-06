#ifndef SONAR_SOCKET_H
#define SONAR_SOCKET_H
#include "FreeRTOS.h"
#include <stdbool.h>
#include <stdint.h>

/* Non-blocking lwIP socket helpers shared by the TCP 5001/5003/5004 servers
 * and UDP discovery. Call from tasks only. */
typedef enum {
    SONAR_SOCKET_DONE,
    SONAR_SOCKET_CLOSED, /* Peer closed the connection. */
    SONAR_SOCKET_FAILED, /* Socket error; errno holds the lwIP error. */
    SONAR_SOCKET_TIMEOUT
} sonar_socket_status_t;

/* Bound, non-blocking socket on INADDR_ANY:port; listening for SOCK_STREAM.
 * Returns -1 on failure. */
int sonar_socket_open(int type, uint16_t port);
bool sonar_socket_nonblocking(int fd);
bool sonar_socket_would_block(int error);

/* Send or receive exactly `length` bytes before `timeout_ms` has elapsed since
 * `began`. While the socket would block, `idle` runs (if not NULL) and the task
 * sleeps one tick. `done` (optional) receives the bytes transferred. */
sonar_socket_status_t sonar_socket_transfer(int fd, uint8_t *data, uint32_t length, bool sending,
                                            TickType_t began, uint32_t timeout_ms,
                                            void (*idle)(void), uint32_t *done);
#endif
