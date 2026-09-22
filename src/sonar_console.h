#ifndef SONAR_CONSOLE_H
#define SONAR_CONSOLE_H
#include <stdbool.h>
#include <stdint.h>
/* Create before any application task; use only from tasks, never from an ISR. */
bool sonar_console_create(void);
void sonar_console_lock(void);
void sonar_console_unlock(void);
/* Task-only, bounded UART packet write; diagnostics can run between packets. */
bool sonar_console_write_packet(const uint8_t *data, uint32_t bytes);

/* Single UART reader. Only the input task touches the UART RX FIFO; every
 * other task receives its keys through a queue, so no keystroke is "stolen".
 * Routing: 'r'/'d' -> mic, 'f'/'b'/'c'/'u'/'m' -> motor, 's'/'x' -> both. */
typedef enum { SONAR_INPUT_MIC = 0, SONAR_INPUT_MOTOR = 1 } sonar_input_target_t;
bool sonar_console_input_create(void);
bool sonar_console_input_take(sonar_input_target_t target, uint8_t *key);
#endif
