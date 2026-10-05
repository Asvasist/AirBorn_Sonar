#ifndef SONAR_CONSOLE_BUFFER_H
#define SONAR_CONSOLE_BUFFER_H
#include <stddef.h>
#include <stdint.h>
#define SONAR_CONSOLE_BUFFER_BYTES 65536U
typedef struct {
    uint8_t data[SONAR_CONSOLE_BUFFER_BYTES];
    uint64_t written, complete;
} sonar_console_buffer_t;
/* Caller serializes access. Zero initialization is sufficient, even pre-RTOS. */
void sonar_console_buffer_put(sonar_console_buffer_t *, uint8_t);
size_t sonar_console_buffer_read(const sonar_console_buffer_t *, uint64_t *cursor,
                                uint64_t through, uint8_t *, size_t, uint64_t *lost);
#endif
