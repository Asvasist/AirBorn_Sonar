#ifndef SONAR_IDENTITY_H
#define SONAR_IDENTITY_H
#include <stdbool.h>
#include <stdint.h>
#define SONAR_ID_WORDS 4U
bool sonar_identity_set_boot(const uint32_t id[SONAR_ID_WORDS]);
bool sonar_identity_prepare_test(const uint32_t boot[SONAR_ID_WORDS],
                                 const uint32_t test[SONAR_ID_WORDS]);
bool sonar_identity_test_ready(void);
void sonar_identity_consume_test(void);
void sonar_identity_disarm(void);
void sonar_identity_snapshot(uint32_t boot[SONAR_ID_WORDS], uint32_t test[SONAR_ID_WORDS]);
#endif
