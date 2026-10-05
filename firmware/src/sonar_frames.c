#include "sonar_frames.h"
#include <string.h>
int sonar_frames_acquire(sonar_frames_t *p)
{
    for (unsigned i=0; i<SONAR_ACQ_POOL_COUNT; ++i) {
        if (p->slots[i].state == FRAME_FREE) { p->slots[i].state=FRAME_FILLING; return (int)i; }
    }
    return -1;
}
bool sonar_frames_publish(sonar_frames_t *p, unsigned i, const sonar_frame_meta_t *m)
{
    if (i>=SONAR_ACQ_POOL_COUNT || p->slots[i].state!=FRAME_FILLING) { return false; }
    p->slots[i].meta=*m; p->slots[i].state=FRAME_READY; return true;
}
int sonar_frames_take(sonar_frames_t *p, uint32_t through)
{
    int chosen=-1; uint32_t oldest=UINT32_MAX;
    for (unsigned i=0; i<SONAR_ACQ_POOL_COUNT; ++i) {
        sonar_frame_slot_t *s=&p->slots[i];
        if (s->state==FRAME_READY && s->meta.sequence<=through &&
            (chosen<0 || s->meta.sequence<oldest)) { chosen=(int)i; oldest=s->meta.sequence; }
    }
    if (chosen>=0) { p->slots[chosen].state=FRAME_SENDING; }
    return chosen;
}
bool sonar_frames_finish(sonar_frames_t *p, unsigned i, bool ack)
{
    if (i>=SONAR_ACQ_POOL_COUNT || p->slots[i].state!=FRAME_SENDING) { return false; }
    p->slots[i].state=ack?FRAME_FREE:FRAME_READY; return true;
}
uint32_t sonar_frames_latest(const sonar_frames_t *p)
{
    uint32_t n=0;
    for (unsigned i=0; i<SONAR_ACQ_POOL_COUNT; ++i) {
        if (p->slots[i].state==FRAME_READY && p->slots[i].meta.sequence>n) { n=p->slots[i].meta.sequence; }
    }
    return n;
}
uint32_t sonar_crc32(const uint8_t *p, uint32_t n)
{
    uint32_t crc=UINT32_MAX;
    for (uint32_t i=0; i<n; ++i) {
        crc^=p[i];
        for (unsigned b=0; b<8; ++b) { crc=(crc>>1) ^ (0xedb88320U & (0U-(crc&1U))); }
    }
    return ~crc;
}
static void put(uint8_t *p, uint64_t n, unsigned bytes)
{ for (unsigned i=0;i<bytes;++i) { p[i]=(uint8_t)(n>>(i*8U)); } }
void sonar_frame_header(uint8_t out[SONAR_WIRE_HEADER], const sonar_frame_meta_t *m, uint32_t bytes, uint32_t crc)
{
    memset(out,0,SONAR_WIRE_HEADER); memcpy(out,"ASN2",4);
    put(out+4,1,2); put(out+6,SONAR_WIRE_HEADER,2); put(out+8,bytes,4);
    put(out+12,m->sequence,4); put(out+16,m->sequence,4); put(out+20,m->flags,4);
    put(out+24,SONAR_ACQ_CHANNELS,4); put(out+28,SONAR_ACQ_PDM_HZ,4);
    put(out+32,m->trigger_us,8); put(out+40,m->completion_us,8); put(out+48,(uint64_t)m->position,8);
    put(out+56,(uint32_t)m->steps,4); put(out+60,m->divisor,4);
    put(out+64,m->tx_duration_us,4);
    put(out+68,(uint64_t)SONAR_ACQ_WORDS*2U*1000000U/SONAR_ACQ_PDM_HZ,4);
    put(out+72,crc,4); put(out+76,sonar_crc32(out,76),4);
}
