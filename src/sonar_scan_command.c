#include "sonar_scan_command.h"
#include <stddef.h>
#include <string.h>

/* Parse fixed-point quantities without floating point or locale dependencies. */
static bool number(const char *s, unsigned precision, bool signed_value, int64_t *value)
{
    bool negative = false, digit = false;
    uint64_t whole = 0U, fraction = 0U, scale = 1U;
    unsigned places = 0U;
    if (*s == '-' || *s == '+') {
        if (!signed_value) { return false; }
        negative = *s++ == '-';
    }
    while (*s >= '0' && *s <= '9') {
        digit = true; whole = whole * 10U + (unsigned)(*s++ - '0');
        if (whole > UINT32_MAX) { return false; }
    }
    if (!digit) { return false; }
    if (*s == '.') {
        ++s;
        while (*s >= '0' && *s <= '9') {
            if (++places > precision) { return false; }
            fraction = fraction * 10U + (unsigned)(*s++ - '0');
        }
        if (places == 0U) { return false; }
    }
    if (*s != '\0') { return false; }
    for (unsigned i = 0U; i < precision; ++i) { scale *= 10U; }
    for (unsigned i = places; i < precision; ++i) { fraction *= 10U; }
    int64_t result = (int64_t)(whole * scale + fraction);
    if (negative) { result = -result; }
    if (signed_value ? (result < INT32_MIN || result > INT32_MAX) : (result < 0 || result > UINT32_MAX)) { return false; }
    *value = result; return true;
}
static bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
sonar_scan_command_t sonar_scan_command(sonar_scan_config_t *config, const char *line)
{
    char buffer[128], *words[3]; size_t length = 0U; unsigned count = 0U;
    if (config == NULL || line == NULL) { return SCAN_CMD_INVALID; }
    while (line[length] != '\0') { if (length == sizeof(buffer) - 1U) { return SCAN_CMD_INVALID; } ++length; }
    memcpy(buffer, line, length + 1U);
    char *p = buffer;
    while (*p != '\0') {
        while (space(*p)) { ++p; }
        if (*p == '\0') { break; }
        if (count == 3U) { return SCAN_CMD_INVALID; }
        words[count++] = p;
        while (*p != '\0' && !space(*p)) { ++p; }
        if (*p != '\0') { *p++ = '\0'; }
    }
    if (count == 1U) {
        static const char *const actions[] = {"show", "check", "simulate", "run", "cancel", "help"};
        for (unsigned i = 0U; i < sizeof(actions)/sizeof(actions[0]); ++i) {
            if (strcmp(words[0], actions[i]) == 0) { return (sonar_scan_command_t)((unsigned)SCAN_CMD_SHOW + i); }
        }
        if (strcmp(words[0], "unreference") == 0) { config->reference_confirmed = false; return SCAN_CMD_UPDATED; }
    }
    if (count == 2U && strcmp(words[0], "mode") == 0) {
        if (strcmp(words[1], "step") == 0) { config->mode = SCAN_STEP_AND_STOP; }
        else if (strcmp(words[1], "continuous") == 0) { config->mode = SCAN_CONTINUOUS; }
        else { return SCAN_CMD_INVALID; }
        return SCAN_CMD_UPDATED;
    }
    int64_t value;
    if (count == 2U && strcmp(words[0], "reference") == 0 && number(words[1], 0U, true, &value)) {
        config->reference_steps = (int32_t)value; config->reference_confirmed = true; return SCAN_CMD_UPDATED;
    }
    if (count != 3U || strcmp(words[0], "set") != 0) { return SCAN_CMD_INVALID; }
    if (strcmp(words[1], "speed") == 0) {
        if (!number(words[2], 4U, true, &value) || value < -500000000 || value > 500000000) { return SCAN_CMD_INVALID; }
        config->velocity = (int32_t)value; return SCAN_CMD_UPDATED;
    }
    if (strcmp(words[1], "start_deg") == 0 || strcmp(words[1], "step_deg") == 0) {
        if (!number(words[2], 3U, true, &value)) { return SCAN_CMD_INVALID; }
        if (strcmp(words[1], "start_deg") == 0) { config->start_angle_mdeg = (int32_t)value; }
        else { config->step_angle_mdeg = (int32_t)value; }
        return SCAN_CMD_UPDATED;
    }
    if (!number(words[2], 0U, false, &value)) { return SCAN_CMD_INVALID; }
    const struct { const char *name; uint32_t *field; } fields[] = {
        {"shots", &config->shots}, {"capture_bytes", &config->capture_bytes},
        {"steps_per_rev", &config->steps_per_revolution}, {"interval_ms", &config->interval_ms},
        {"settle_ms", &config->settle_ms}, {"motion_timeout_ms", &config->motion_timeout_ms},
        {"capture_timeout_ms", &config->capture_timeout_ms}, {"stop_timeout_ms", &config->stop_timeout_ms},
        {"frame_hold_timeout_ms", &config->frame_hold_timeout_ms}
    };
    for (unsigned i = 0U; i < sizeof(fields)/sizeof(fields[0]); ++i) {
        if (strcmp(words[1], fields[i].name) == 0) { *fields[i].field = (uint32_t)value; return SCAN_CMD_UPDATED; }
    }
    return SCAN_CMD_INVALID;
}
