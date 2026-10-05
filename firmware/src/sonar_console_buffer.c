#include "sonar_console_buffer.h"
void sonar_console_buffer_put(sonar_console_buffer_t *b, uint8_t value)
{
    b->data[b->written % SONAR_CONSOLE_BUFFER_BYTES] = value;
    ++b->written;
    if (value == '\n') { b->complete = b->written; }
}
size_t sonar_console_buffer_read(const sonar_console_buffer_t *b, uint64_t *cursor,
                                uint64_t through, uint8_t *out, size_t capacity, uint64_t *lost)
{
    uint64_t oldest = b->written > SONAR_CONSOLE_BUFFER_BYTES ?
                      b->written - SONAR_CONSOLE_BUFFER_BYTES : 0U;
    *lost = 0U;
    if (*cursor < oldest) {
        *lost = oldest - *cursor;
        *cursor = oldest;
        return 0U; /* Report the gap before returning more text. */
    }
    uint64_t end = through < b->complete ? through : b->complete;
    if (*cursor >= end) { return 0U; }
    size_t count = end - *cursor < capacity ? (size_t)(end - *cursor) : capacity;
    for (size_t i = 0U; i < count; ++i) {
        out[i] = b->data[(*cursor + i) % SONAR_CONSOLE_BUFFER_BYTES];
    }
    *cursor += count;
    return count;
}
