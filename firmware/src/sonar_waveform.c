#include "sonar_waveform.h"
#include "sonar_tx_bram.h"
#include "sonar_control_protocol.h"
#include <stddef.h>
#include <string.h>

typedef enum { RIFF_HEADER, CHUNK_HEADER, FORMAT, AUDIO_DATA, SKIP, PAD } phase_t;
static struct {
    phase_t phase;
    uint8_t header[12], fmt[40], frame[8];
    uint32_t used, fmt_used, frame_used, remaining, chunk_bytes;
    uint32_t format, channels, bits, block, samples, pcm_crc;
    uint32_t file_bytes, expected_crc, file_crc, received, pending_word;
    bool fmt_seen, data_seen;
} parser;
static uint32_t current_id, next_id, current_samples, current_crc;
static bool uploading, locked;
static uint32_t crc_byte(uint32_t crc, uint8_t b)
{
    crc^=b;
    for (unsigned i=0;i<8U;++i) { crc=(crc>>1U)^(0xedb88320U & (0U-(crc&1U))); }
    return crc;
}
static uint32_t u16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8U; }
static void chunk_done(void)
{ parser.phase=(parser.chunk_bytes & 1U)!=0U?PAD:CHUNK_HEADER; parser.used=0U; }
static bool format_done(void)
{
    if (parser.fmt_used<16U) { return false; }
    parser.format=u16(parser.fmt); parser.channels=u16(parser.fmt+2);
    parser.block=u16(parser.fmt+12); parser.bits=u16(parser.fmt+14);
    if (parser.format==65534U) {
        static const uint8_t guid_tail[12]={0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
        if (parser.fmt_used<40U || u16(parser.fmt+16)<22U ||
            u16(parser.fmt+18)!=parser.bits || memcmp(parser.fmt+28,guid_tail,12)!=0) { return false; }
        parser.format=sonar_get_u32(parser.fmt+24);
    }
    if ((parser.channels!=1U && parser.channels!=2U) || sonar_get_u32(parser.fmt+4)!=SONAR_AUDIO_HZ ||
        (parser.format!=1U && parser.format!=3U) ||
        (parser.format==3U && parser.bits!=32U) ||
        (parser.bits!=8U && parser.bits!=16U && parser.bits!=24U && parser.bits!=32U) ||
        parser.block!=parser.channels*(parser.bits/8U) ||
        sonar_get_u32(parser.fmt+8)!=SONAR_AUDIO_HZ*parser.block) { return false; }
    parser.fmt_seen=true; return true;
}
static bool sample(const uint8_t *p, int32_t *out)
{
    uint32_t width=parser.bits/8U, raw=0;
    for (uint32_t i=0;i<width;++i) { raw|=(uint32_t)p[i]<<(8U*i); }
    if (parser.format==3U) {
        if ((raw & 0x7f800000U)==0x7f800000U) { return false; } /* NaN/Inf */
        /* Decode IEEE binary32 using integers. This task needs no FPU context
         * in the supplied FreeRTOS port (configUSE_TASK_FPU_SUPPORT=1). */
        uint32_t magnitude=raw & 0x7fffffffU;
        bool negative=(raw & 0x80000000U)!=0U;
        if (magnitude>=0x3f800000U) { *out=negative?-32768:32767; }
        else {
            uint32_t exponent=magnitude>>23U;
            uint32_t pcm=exponent<112U?0U:((magnitude & 0x7fffffU)|0x800000U)>>(135U-exponent);
            *out=negative?-(int32_t)pcm:(int32_t)pcm;
        }
    } else if (parser.bits==8U) { *out=((int32_t)raw-128)*256; }
    else {
        int64_t x=(int64_t)raw;
        if ((raw & (UINT32_C(1)<<(parser.bits-1U)))!=0U) { x-=INT64_C(1)<<parser.bits; }
        *out=(int32_t)(x/(INT64_C(1)<<(parser.bits-16U)));
    }
    return true;
}
static bool emit_frame(void)
{
    int32_t sum=0;
    for (uint32_t ch=0;ch<parser.channels;++ch) {
        int32_t value;
        if (!sample(parser.frame+ch*(parser.bits/8U),&value)) { return false; }
        sum+=value;
    }
    uint16_t pcm=(uint16_t)(int16_t)(sum/(int32_t)parser.channels);
    if (parser.samples>=sonar_tx_bram_capacity()) { return false; }
    parser.pcm_crc=crc_byte(crc_byte(parser.pcm_crc,(uint8_t)pcm),(uint8_t)(pcm>>8U));
    if ((parser.samples & 1U)==0U) { parser.pending_word=pcm; }
    else if (!sonar_tx_bram_write(parser.samples/2U,parser.pending_word|((uint32_t)pcm<<16U))) { return false; }
    ++parser.samples; parser.frame_used=0;
    return true;
}
static bool feed(uint8_t byte)
{
    switch (parser.phase) {
    case RIFF_HEADER:
        parser.header[parser.used++]=byte;
        if (parser.used==12U) {
            if (memcmp(parser.header,"RIFF",4)!=0 || memcmp(parser.header+8,"WAVE",4)!=0 ||
                (uint64_t)sonar_get_u32(parser.header+4)+8U!=parser.file_bytes) { return false; }
            parser.phase=CHUNK_HEADER; parser.used=0;
        }
        break;
    case CHUNK_HEADER:
        parser.header[parser.used++]=byte;
        if (parser.used==8U) {
            uint32_t n=sonar_get_u32(parser.header+4);
            if ((uint64_t)n+(n&1U)>parser.file_bytes-parser.received) { return false; }
            parser.remaining=n; parser.chunk_bytes=n; parser.used=0;
            if (memcmp(parser.header,"fmt ",4)==0) {
                if (parser.fmt_seen || n<16U) { return false; }
                parser.fmt_used=0; parser.phase=FORMAT;
            } else if (memcmp(parser.header,"data",4)==0) {
                if (!parser.fmt_seen || parser.data_seen || n==0U || n%parser.block!=0U || n/parser.block>sonar_tx_bram_capacity()) { return false; }
                parser.phase=AUDIO_DATA;
            } else { parser.phase=SKIP; }
            if (n==0U) { chunk_done(); }
        }
        break;
    case FORMAT:
        if (parser.fmt_used<sizeof(parser.fmt)) { parser.fmt[parser.fmt_used++]=byte; }
        if (--parser.remaining==0U) {
            if (!format_done()) { return false; }
            chunk_done();
        }
        break;
    case AUDIO_DATA:
        parser.frame[parser.frame_used++]=byte;
        if (parser.frame_used==parser.block && !emit_frame()) { return false; }
        if (--parser.remaining==0U) { parser.data_seen=true; chunk_done(); }
        break;
    case SKIP: if (--parser.remaining==0U) { chunk_done(); } break;
    case PAD: parser.phase=CHUNK_HEADER; parser.used=0; break;
    default: return false;
    }
    return true;
}
bool sonar_waveform_begin(uint32_t bytes, uint32_t crc)
{
    if (locked || uploading || bytes<44U || bytes>SONAR_WAV_FILE_MAX_BYTES || next_id==UINT32_MAX ||
        !sonar_tx_bram_init()) { return false; }
    /* One playback buffer: invalidate the old waveform before any overwrite.
     * Failed or cancelled uploads must never replay partly replaced samples. */
    current_id=current_samples=current_crc=0U;
    memset(&parser,0,sizeof(parser)); parser.phase=RIFF_HEADER;
    parser.file_bytes=bytes; parser.expected_crc=crc;
    parser.file_crc=parser.pcm_crc=UINT32_MAX;
    uploading=true; return true;
}
bool sonar_waveform_append(uint32_t offset, const uint8_t *data, uint32_t bytes)
{
    if (locked || !uploading || data==NULL || bytes==0U || offset!=parser.received ||
        bytes>parser.file_bytes-parser.received) { return false; }
    for (uint32_t i=0;i<bytes;++i) {
        ++parser.received;
        parser.file_crc=crc_byte(parser.file_crc,data[i]);
        if (!feed(data[i])) { uploading=false; return false; }
    }
    return true;
}
bool sonar_waveform_commit(void)
{
    if (locked || !uploading || parser.received!=parser.file_bytes || !parser.fmt_seen || !parser.data_seen ||
        parser.phase!=CHUNK_HEADER || parser.used!=0U || parser.frame_used!=0U || parser.samples==0U) { return false; }
    if (~parser.file_crc!=parser.expected_crc) { uploading=false; return false; }
    if ((parser.samples & 1U)!=0U && !sonar_tx_bram_write(parser.samples/2U,parser.pending_word)) { return false; }
    current_id=++next_id; current_samples=parser.samples; current_crc=~parser.pcm_crc;
    uploading=false; return true;
}
void sonar_waveform_cancel(void) { uploading=false; parser.received=0; }
void sonar_waveform_lock(bool value) { locked=value; }
bool sonar_waveform_uploading(void) { return uploading; }
uint32_t sonar_waveform_received(void) { return parser.received; }
uint32_t sonar_waveform_id(void) { return current_id; }
uint32_t sonar_waveform_samples(void) { return current_samples; }
uint32_t sonar_waveform_crc(void) { return current_crc; }
