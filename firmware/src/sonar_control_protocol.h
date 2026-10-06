#ifndef SONAR_CONTROL_PROTOCOL_H
#define SONAR_CONTROL_PROTOCOL_H
#include <stdbool.h>
#include <stdint.h>
#define SONAR_CONTROL_PORT        5003U
#define SONAR_CONTROL_HEADER      28U
#define SONAR_CONTROL_MAX_PAYLOAD 1024U
#define SONAR_CONTROL_REPLY_WORDS 22U
#define SONAR_CONTROL_REPLY_BYTES (SONAR_CONTROL_REPLY_WORDS * 4U)
#define SONAR_CONTROL_RESPONSE    UINT32_C(0x80000000)
enum {
    CTRL_GET_CAPABILITIES = 1,
    CTRL_SET_GENERATED,
    CTRL_UPLOAD_BEGIN,
    CTRL_UPLOAD_DATA,
    CTRL_UPLOAD_COMMIT,
    CTRL_SELECT_WAVEFORM,
    CTRL_APPLY,
    CTRL_GET_CONFIGURATION,
    CTRL_UPLOAD_CANCEL,
    CTRL_END_SESSION = 65535
};
enum {
    CTRL_OK = 0,
    CTRL_BAD_REQUEST,
    CTRL_BUSY,
    CTRL_RANGE,
    CTRL_CHECKSUM,
    CTRL_NO_WAVEFORM,
    CTRL_HARDWARE,
    CTRL_TIMEOUT,
    CTRL_SEQUENCE
};
uint32_t sonar_get_u32(const uint8_t *);
void sonar_put_u32(uint8_t *, uint32_t);
void sonar_control_header(uint8_t out[SONAR_CONTROL_HEADER], uint32_t opcode, uint32_t request,
                          const uint8_t *payload, uint32_t bytes);
bool sonar_control_decode(const uint8_t header[SONAR_CONTROL_HEADER], uint32_t *opcode,
                          uint32_t *request, uint32_t *bytes, uint32_t *crc);
#endif
