#ifndef SONAR_TCP_CONSOLE_H
#define SONAR_TCP_CONSOLE_H
#include <stdbool.h>
#define SONAR_CONSOLE_PORT 5004U
/* Optional UART diagnostic copy; the GUI never opens a serial port. */
#ifndef SONAR_CONSOLE_MIRROR_UART
#define SONAR_CONSOLE_MIRROR_UART 1
#endif
bool sonar_tcp_console_start(void);
#endif
