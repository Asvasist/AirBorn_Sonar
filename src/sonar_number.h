#ifndef SONAR_NUMBER_H
#define SONAR_NUMBER_H
#include <stdbool.h>
#include <stdint.h>
typedef struct { char text[16]; unsigned length; bool invalid; } sonar_number_t;
typedef enum { NUMBER_WAIT, NUMBER_OK, NUMBER_INVALID } sonar_number_result_t;
sonar_number_result_t sonar_number_feed(sonar_number_t *, uint8_t key, int32_t *value);
#endif
