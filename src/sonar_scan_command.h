#ifndef SONAR_SCAN_COMMAND_H
#define SONAR_SCAN_COMMAND_H
#include "sonar_scan_config.h"
typedef enum { SCAN_CMD_INVALID, SCAN_CMD_UPDATED, SCAN_CMD_SHOW, SCAN_CMD_CHECK,
    SCAN_CMD_SIMULATE, SCAN_CMD_RUN, SCAN_CMD_CANCEL, SCAN_CMD_HELP } sonar_scan_command_t;
/* Full command line, maximum 127 characters. Invalid edits preserve the plan.
 * Configuration is edited outside a running coordinator and copied at init. */
sonar_scan_command_t sonar_scan_command(sonar_scan_config_t *config, const char *line);
#endif
