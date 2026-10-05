#include "sonar_experiment.h"
#include "sonar_chirp.h"
#include "sonar_waveform.h"
#include "sonar_tx_bram.h"
#include <string.h>

static sonar_experiment_config_t active, pending;
static sonar_chirp_values_t values;
static bool valid, staged, running;
static uint32_t owner, next_id;

void sonar_experiment_reply(uint32_t error, uint32_t r[SONAR_CONTROL_REPLY_WORDS])
{
    memset(r,0,SONAR_CONTROL_REPLY_BYTES);
    r[0]=error; r[1]=sonar_chirp_capabilities(); r[2]=valid;
    r[3]=active.config_id; r[4]=(uint32_t)active.mode;
    r[5]=active.start_hz; r[6]=active.stop_hz; r[7]=values.duration_us;
    r[8]=active.amplitude_pct; r[9]=active.waveform_id; r[10]=values.total_samples;
    r[11]=sonar_waveform_id(); r[12]=sonar_waveform_samples(); r[13]=sonar_waveform_received();
    r[14]=SONAR_AUDIO_HZ;
    /* Existing wire field: WAV buffer capacity when WAV is available.
     * Generated chirps retain their independent 49 ms limit. */
    r[15]=(r[1]&2U)?sonar_tx_bram_capacity():SONAR_TX_MAX_SAMPLES;
    r[16]=SONAR_CAPTURE_US;
    r[17]=SONAR_TX_START_MARGIN_US; r[18]=sonar_waveform_crc();
    r[19]=sonar_chirp_status(); r[20]=(staged?1U:0U)|(sonar_waveform_uploading()?2U:0U)|(running?4U:0U);
    r[21]=12000U; /* Current RX FIR cutoff, not a flat-passband guarantee. */
}
bool sonar_experiment_can_start(void)
{
    return valid && !staged && !sonar_waveform_uploading() && !running &&
        (active.mode!=SONAR_TX_WAV || active.waveform_id==sonar_waveform_id());
}
void sonar_experiment_running(bool value) { running=value; sonar_waveform_lock(value); }
const sonar_experiment_config_t *sonar_experiment_active(void) { return &active; }
const sonar_chirp_values_t *sonar_experiment_values(void) { return &values; }
static uint32_t command(uint32_t session, uint32_t op, const uint8_t *p, uint32_t n, bool mutable)
{
    if (op==CTRL_GET_CAPABILITIES || op==CTRL_GET_CONFIGURATION) { return n==0U?CTRL_OK:CTRL_BAD_REQUEST; }
    if (op==CTRL_END_SESSION) {
        if (session==owner) { staged=false; owner=0U; sonar_waveform_cancel(); }
        return CTRL_OK;
    }
    if (!mutable || running) { return CTRL_BUSY; }
    if (session==0U || (owner!=0U && owner!=session)) { return CTRL_BUSY; }
    uint32_t error=CTRL_OK;
    sonar_experiment_config_t candidate={0};
    sonar_chirp_values_t calculated;
    switch (op) {
    case CTRL_SET_GENERATED:
        if (n!=16U) { return CTRL_BAD_REQUEST; }
        candidate=(sonar_experiment_config_t){.mode=SONAR_TX_GENERATE,
            .start_hz=sonar_get_u32(p),.stop_hz=sonar_get_u32(p+4),
            .duration_us=sonar_get_u32(p+8),.amplitude_pct=sonar_get_u32(p+12)};
        if (!sonar_experiment_calculate(&candidate,0U,&calculated)) { return CTRL_RANGE; }
        pending=candidate; staged=true; owner=session;
        break;
    case CTRL_UPLOAD_BEGIN:
        if (n!=8U) { return CTRL_BAD_REQUEST; }
        if ((sonar_chirp_capabilities() & 2U)==0U) { return CTRL_HARDWARE; }
        if (!sonar_waveform_begin(sonar_get_u32(p),sonar_get_u32(p+4))) { return CTRL_RANGE; }
        if (active.mode==SONAR_TX_WAV) { valid=false; }
        staged=false;
        owner=session;
        break;
    case CTRL_UPLOAD_DATA:
        if (n<=4U || owner!=session) { return CTRL_BAD_REQUEST; }
        if (!sonar_waveform_append(sonar_get_u32(p),p+4,n-4U)) { error=CTRL_RANGE; }
        break;
    case CTRL_UPLOAD_COMMIT:
        if (n!=0U || owner!=session) { return CTRL_BAD_REQUEST; }
        if (!sonar_waveform_commit()) { return CTRL_CHECKSUM; }
        if (active.mode==SONAR_TX_WAV) { valid=false; }
        break;
    case CTRL_SELECT_WAVEFORM:
        if (n!=8U) { return CTRL_BAD_REQUEST; }
        candidate=(sonar_experiment_config_t){.mode=SONAR_TX_WAV,
            .waveform_id=sonar_get_u32(p),.amplitude_pct=sonar_get_u32(p+4)};
        if (candidate.waveform_id==0U || candidate.waveform_id!=sonar_waveform_id()) { return CTRL_NO_WAVEFORM; }
        if (!sonar_experiment_calculate(&candidate,sonar_waveform_samples(),&calculated)) { return CTRL_RANGE; }
        pending=candidate; staged=true; owner=session;
        break;
    case CTRL_APPLY:
        if (n!=0U || !staged || owner!=session) { return CTRL_BAD_REQUEST; }
        if (sonar_waveform_uploading()) { return CTRL_BUSY; }
        if (pending.mode==SONAR_TX_WAV && pending.waveform_id!=sonar_waveform_id()) { return CTRL_NO_WAVEFORM; }
        if (!sonar_experiment_calculate(&pending,sonar_waveform_samples(),&calculated)) { return CTRL_RANGE; }
        /* Upper 16 recording-header flag bits carry config_id. Never wrap. */
        if (next_id>=65535U) { return CTRL_SEQUENCE; }
        pending.config_id=++next_id;
        valid=false;
        if (!sonar_chirp_apply(&pending,&calculated)) { return CTRL_HARDWARE; }
        active=pending; values=calculated;
        active.duration_us=values.duration_us;
        valid=true; staged=false;
        break;
    case CTRL_UPLOAD_CANCEL:
        if (n!=0U) { return CTRL_BAD_REQUEST; }
        sonar_waveform_cancel();
        break;
    default: error=CTRL_BAD_REQUEST; break;
    }
    return error;
}
void sonar_experiment_command(uint32_t session, uint32_t op, const uint8_t *data,
                              uint32_t bytes, bool mutable, uint32_t reply[SONAR_CONTROL_REPLY_WORDS])
{ sonar_experiment_reply(command(session,op,data,bytes,mutable),reply); }
