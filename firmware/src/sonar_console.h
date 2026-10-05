#ifndef SONAR_CONSOLE_H
#define SONAR_CONSOLE_H
#include <stdbool.h>
#include <stdint.h>
/* Create before any application task; use only from tasks, never from an ISR. */
bool sonar_console_create(void);
void sonar_console_lock(void);
void sonar_console_unlock(void);

#endif
