#include "sonar_identity.h"
#include <stddef.h>
#include <string.h>
/* Experiment-task ownership only. Reset clears these RAM variables.
 * The laptop supplies a new random UUID when the boot identity is zero. */
static uint32_t boot_id[SONAR_ID_WORDS];
static uint32_t test_id[SONAR_ID_WORDS];
static bool prepared;
static bool nonzero(const uint32_t *id)
{
    return id != NULL && (id[0] | id[1] | id[2] | id[3]) != 0U;
}
bool sonar_identity_set_boot(const uint32_t id[SONAR_ID_WORDS])
{
    if (!nonzero(id)) {
        return false;
    }
    if (nonzero(boot_id)) {
        return memcmp(id, boot_id, sizeof(boot_id)) == 0;
    }
    memcpy(boot_id, id, sizeof(boot_id));
    return true;
}
bool sonar_identity_prepare_test(const uint32_t boot[SONAR_ID_WORDS],
                                 const uint32_t test[SONAR_ID_WORDS])
{
    if (!nonzero(boot) || !nonzero(test) || memcmp(boot, boot_id, sizeof(boot_id)) != 0 ||
        memcmp(test, test_id, sizeof(test_id)) == 0) {
        return false;
    }
    memcpy(test_id, test, sizeof(test_id));
    prepared = true;
    return true;
}
bool sonar_identity_test_ready(void)
{
    return prepared;
}
void sonar_identity_consume_test(void)
{
    prepared = false;
}
void sonar_identity_disarm(void)
{
    prepared = false;
}
void sonar_identity_snapshot(uint32_t boot[SONAR_ID_WORDS], uint32_t test[SONAR_ID_WORDS])
{
    memcpy(boot, boot_id, sizeof(boot_id));
    memcpy(test, test_id, sizeof(test_id));
}
