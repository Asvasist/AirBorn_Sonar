#include "sonar_control_protocol.h"
#include "sonar_frames.h"
#include <string.h>
uint32_t sonar_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8U | (uint32_t)p[2] << 16U | (uint32_t)p[3] << 24U;
}
void sonar_put_u32(uint8_t *p, uint32_t n)
{
    for (unsigned i = 0; i < 4U; ++i) {
        p[i] = (uint8_t)(n >> (i * 8U));
    }
}
void sonar_control_header(uint8_t out[SONAR_CONTROL_HEADER], uint32_t op, uint32_t id,
                          const uint8_t *payload, uint32_t bytes)
{
    memset(out, 0, SONAR_CONTROL_HEADER);
    memcpy(out, "ASC1", 4);
    out[4] = 1;
    out[6] = SONAR_CONTROL_HEADER;
    sonar_put_u32(out + 8, op);
    sonar_put_u32(out + 12, id);
    sonar_put_u32(out + 16, bytes);
    sonar_put_u32(out + 20, sonar_crc32(payload, bytes));
    sonar_put_u32(out + 24, sonar_crc32(out, 24));
}
bool sonar_control_decode(const uint8_t h[SONAR_CONTROL_HEADER], uint32_t *op, uint32_t *id,
                          uint32_t *bytes, uint32_t *crc)
{
    if (memcmp(h, "ASC1", 4) != 0 || h[4] != 1U || h[5] != 0U || h[6] != SONAR_CONTROL_HEADER ||
        h[7] != 0U || sonar_get_u32(h + 24) != sonar_crc32(h, 24) ||
        sonar_get_u32(h + 16) > SONAR_CONTROL_MAX_PAYLOAD) {
        return false;
    }
    *op = sonar_get_u32(h + 8);
    *id = sonar_get_u32(h + 12);
    *bytes = sonar_get_u32(h + 16);
    *crc = sonar_get_u32(h + 20);
    return true;
}
