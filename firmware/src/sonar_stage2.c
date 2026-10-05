#include "sonar_stage2.h"
#include "sonar_number.h"
#include "sonar_cycle.h"
#include "sonar_stepper.h"
#include "sonar_scan360.h"
#include "sonar_motor_zynq.h"
#include "sonar_motor_config.h"
#include "sonar_mic_zynq.h"
#include "sonar_speaker_board.h"
#include "sonar_console.h"
#include "sonar_clock.h"
#include "sonar_rtos.h"
#include "sonar_chirp.h"
#include "sonar_experiment.h"
#include "sonar_waveform.h"
#include "sonar_control_server.h"
#include "sonar_audio_hw.h"
#include "sonar_wav_player.h"
#include "sonar_tx_bram.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "lwip/sys.h"
#include "xil_printf.h"

sonar_frames_t sonar_frames;
uint8_t sonar_frame_data[SONAR_STAGE2_POOL_COUNT][SONAR_STAGE2_SPAN] __attribute__((aligned(32)));
static QueueHandle_t keys;
static TaskHandle_t experiment_handle;
static bool stop_requested, inhibited, input_overflow;
static sonar_cycle_t cycle;
static sonar_stepper_t motor;
static sonar_mic_t mic;
static sonar_mic_io_t mic_io;
static sonar_frame_meta_t metadata;
static int filling=-1;
static int32_t requested_positions;
static sonar_scan360_t scan360;
static bool motor_ready;
static bool waiting_for_buffer;
static void message(const char *s);

static uint64_t now_us(void)
{
    return sonar_clock_us();
}
void sonar_stage2_inhibit(void)
{ taskENTER_CRITICAL(); inhibited=true; stop_requested=true; taskEXIT_CRITICAL(); }
void sonar_stage2_key(uint8_t key)
{
    if (key>='A' && key<='Z') { key=(uint8_t)(key-'A'+'a'); }
    if (key=='x') { taskENTER_CRITICAL(); stop_requested=true; taskEXIT_CRITICAL(); }
    else if (keys!=NULL && xQueueSend(keys,&key,0)!=pdPASS) {
        taskENTER_CRITICAL(); input_overflow=true; stop_requested=true; taskEXIT_CRITICAL();
    }
    if (experiment_handle!=NULL) { xTaskNotifyGive(experiment_handle); }
}
static bool stop_outputs(void *context)
{
    (void)context;
    bool muted=sonar_speaker_board_enable(false);
    bool stopped=motor_ready && sonar_stepper_stop(&motor);
    bool tx_stopped=sonar_chirp_abort();
    return muted && stopped && tx_stopped;
}
static bool move_motor(void *context, int32_t unused)
{
    (void)context; (void)unused;
    uint32_t position_index=sonar_scan360_capture_index(&scan360);
    int32_t delta=sonar_scan360_next_delta(&scan360);
    if (position_index==0U || delta==0) { return false; }
    sonar_console_lock();
    xil_printf("CYCLE %u: motor at trigger + %u ms; move %u/%u; delta=%d; revolution=%u\r\n",
        (unsigned)cycle.cycle,(unsigned)((now_us()-cycle.trigger)/1000U),
        (unsigned)position_index,(unsigned)sonar_scan360_positions(&scan360),(int)delta,
        (unsigned)SONAR_SCAN360_REVOLUTION_STEPS);
    sonar_console_unlock();
    if (!sonar_stepper_move(&motor,delta)) { return false; }
    sonar_scan360_mark_move_started(&scan360);
    return true;
}
static int capture(void *context, uint32_t sequence, uint64_t *trigger)
{
    (void)context;
    taskENTER_CRITICAL(); filling=sonar_frames_acquire(&sonar_frames); taskEXIT_CRITICAL();
    if (filling<0) {
        if (!waiting_for_buffer) { message("DDR full: experiment remains armed; next cycle resumes after saved frames are acknowledged."); }
        waiting_for_buffer=true; return 0;
    }
    if (waiting_for_buffer) { message("DDR space available: resuming automatically with the stored positions-per-revolution setting."); }
    waiting_for_buffer=false;
    const sonar_experiment_config_t *tx=sonar_experiment_active();
    const sonar_chirp_values_t *tv=sonar_experiment_values();
    if (!sonar_chirp_prepare()) { return -1; }
    /* A Stop arriving during playback preparation must prevent a later trigger. */
    taskENTER_CRITICAL(); bool cancelled=stop_requested || inhibited; taskEXIT_CRITICAL();
    if (cancelled) {
        bool tx_stopped=sonar_chirp_abort();
        taskENTER_CRITICAL(); sonar_frames.slots[filling].state=FRAME_FREE; taskEXIT_CRITICAL();
        filling=-1;
        return tx_stopped?0:-1;
    }
    sonar_mic_config_t cfg={SONAR_STAGE2_BYTES,
        pdMS_TO_TICKS((SONAR_STAGE2_CAPTURE_TIMEOUT_US+999U)/1000U),SONAR_STAGE2_PDM_HZ};
    if (!sonar_mic_init(&mic,&cfg,&mic_io,sonar_frame_data[filling],SONAR_STAGE2_SPAN) ||
        !sonar_speaker_board_enable(true)) { return -1; }
    metadata=(sonar_frame_meta_t){.sequence=sequence,
        .flags=15U | (tx->config_id<<16U) | (tx->mode==SONAR_TX_WAV?0x200U:0U),
        .divisor=motor.divisor,.steps=sonar_scan360_next_delta(&scan360),.position=motor.status.position,
        .tx_duration_us=tv->duration_us,.config_id=tx->config_id,.tx_mode=(uint32_t)tx->mode,
        .tx_start_hz=tx->start_hz,.tx_stop_hz=tx->stop_hz,.tx_amplitude_pct=tx->amplitude_pct,
        .tx_samples=tv->total_samples,.tx_sample_hz=SONAR_AUDIO_HZ,.waveform_id=tx->waveform_id,
        .waveform_crc=tx->mode==SONAR_TX_WAV?sonar_waveform_crc():0U};
    if (!sonar_mic_start(&mic,(uint32_t)xTaskGetTickCount())) { return -1; }
    /* Use the GPIO-write timestamp, not the end of the one-tick trigger pulse. */
    *trigger=sonar_mic_zynq_trigger_us(); metadata.trigger_us=*trigger;
    sonar_console_lock();
    xil_printf("CYCLE %u: speaker/microphone triggered; scan position %u/%u\r\n",
        (unsigned)sequence,(unsigned)sonar_scan360_capture_index(&scan360),
        (unsigned)sonar_scan360_positions(&scan360));
    xil_printf("TX_CONFIG cycle=%u config=%u mode=%s start_hz=%u stop_hz=%u samples=%u amplitude=%u waveform=%u crc=0x%x tx_sample_hz=96000 rx_sample_hz=96000\r\n",
        (unsigned)sequence,(unsigned)tx->config_id,tx->mode==SONAR_TX_WAV?"WAV":"GENERATE",
        (unsigned)tx->start_hz,(unsigned)tx->stop_hz,(unsigned)tv->total_samples,
        (unsigned)tx->amplitude_pct,(unsigned)tx->waveform_id,(unsigned)metadata.waveform_crc);
    sonar_console_unlock();
    return 1;
}
static void report(void)
{
    unsigned used=0;
    taskENTER_CRITICAL();
    for (unsigned i=0;i<SONAR_STAGE2_POOL_COUNT;++i) {
        if (sonar_frames.slots[i].state!=FRAME_FREE) { ++used; }
    }
    taskEXIT_CRITICAL();
    const sonar_experiment_config_t *tx=sonar_experiment_active();
    const sonar_chirp_values_t *tv=sonar_experiment_values();
    sonar_console_lock();
    xil_printf("STAGE2 %s cycle=%u steps=%d divisor=%u receiver=%u motor_pos=%d DDR=%u/%u config=%u mode=%s tx_us=%u amplitude=%u waveform=%u audio_ready=%u tx_sample_hz=96000 rx_sample_hz=96000\r\n",
        sonar_cycle_name(cycle.state),(unsigned)cycle.cycle,(int)requested_positions,
        (unsigned)motor.divisor,(unsigned)sonar_network_connected(),(int)motor.status.position,
        used,(unsigned)SONAR_STAGE2_POOL_COUNT,(unsigned)tx->config_id,
        tx->mode==SONAR_TX_WAV?"WAV":"GENERATE",(unsigned)tv->duration_us,
        (unsigned)tx->amplitude_pct,(unsigned)tx->waveform_id,(unsigned)sonar_experiment_can_start());
    sonar_console_unlock();
}
static void message(const char *s)
{ sonar_console_lock(); xil_printf("%s\r\n",s); sonar_console_unlock(); }
static bool idle(void)
{ return cycle.state==CYCLE_IDLE || cycle.state==CYCLE_STOPPED; }

static const char *startup_result(bool attempted, bool passed)
{ return !attempted?"SKIP":(passed?"PASS":"FAIL"); }

static void startup_motor_report(void)
{
    const sonar_motor_zynq_diagnostic_t *d=sonar_motor_zynq_diagnostic();
    sonar_console_lock();
    xil_printf("INIT MOTOR: bus=%s Tic=%s divisor=%u status_valid=%u errors=0x%x flags=0x%x\r\n",
        d->init_reason,motor.fault_reason!=NULL?motor.fault_reason:"none",(unsigned)motor.divisor,
        (unsigned)motor.fault_status_valid,(unsigned)motor.fault_status.errors,(unsigned)motor.fault_status.flags);
    xil_printf("INIT MOTOR PS: APER=0x%x RESET=0x%x MIO12=0x%x MIO13=0x%x TRI=0x%x LOOP=0x%x\r\n",
        (unsigned)d->aper,(unsigned)d->reset,(unsigned)d->mio12,(unsigned)d->mio13,
        (unsigned)d->tri,(unsigned)d->loopback);
    if (d->transfer_failed) {
        xil_printf("INIT MOTOR I2C: first_failed=%s address=0x%x command=0x%x bytes=%u IRQ=0x%x SR_after_abort=0x%x quarantined=%u\r\n",
            d->failed_read?"READ":"WRITE",(unsigned)d->failed_address,(unsigned)d->failed_command,
            (unsigned)d->failed_length,(unsigned)d->failed_irq,(unsigned)d->bus_status,(unsigned)d->quarantined);
        if ((d->failed_irq & 4U)!=0U) {
            xil_printf("INIT MOTOR: I2C NACK; verify powered Tic, address 0x0e, SCL/SDA and common ground.\r\n");
        } else {
            xil_printf("INIT MOTOR: I2C transfer failed; inspect IRQ/status, pin routing and bus lines.\r\n");
        }
    }
    sonar_console_unlock();
}

static void experiment_task(void *unused)
{
    (void)unused;
    sonar_tic_io_t motor_io;
    sonar_tic_config_t mc={SONAR_MOTOR_ADDRESS,SONAR_MOTOR_SPEED,SONAR_MOTOR_ACCELERATION,
        SONAR_MOTOR_DECELERATION,0,pdMS_TO_TICKS((SONAR_STAGE2_MOVE_TIMEOUT_US+999U)/1000U),
        pdMS_TO_TICKS(SONAR_MOTOR_KEEPALIVE_MS),pdMS_TO_TICKS(SONAR_MOTOR_WATCHDOG_MS)};
    const sonar_cycle_config_t cc={SONAR_CAPTURE_US+SONAR_TX_TIMEOUT_MARGIN_US,
        SONAR_STAGE2_MOTOR_START_US,SONAR_STAGE2_SETTLE_US,SONAR_STAGE2_CAPTURE_TIMEOUT_US,
        SONAR_STAGE2_MOVE_TIMEOUT_US};
    const sonar_cycle_io_t ci={NULL,capture,move_motor,stop_outputs};
    /* Keep each result: short-circuiting one combined flag hid the failed
     * peripheral, and the run-time fault report skipped initialization faults. */
    bool audio_ok=sonar_chirp_init();
    bool cycle_ok=sonar_cycle_init(&cycle,&cc,&ci);
    bool dma_ok=cycle_ok && sonar_mic_zynq_init(xTaskGetCurrentTaskHandle(),&mic_io);
    bool codec_ok=sonar_speaker_board_init();
    bool codec_begin_ok=codec_ok && sonar_speaker_board_begin((uint32_t)xTaskGetTickCount());
    bool motor_bus_ok=sonar_motor_zynq_init(&motor_io);
    bool tic_ok=motor_bus_ok && sonar_stepper_init(&motor,&mc,&motor_io);
    bool mode_ok=tic_ok && sonar_stepper_mode(&motor,SONAR_STAGE2_DEFAULT_DIVISOR);
    motor_ready=motor_bus_ok && tic_ok && mode_ok;
    bool ok=cycle_ok && dma_ok && codec_ok && codec_begin_ok && motor_ready;
    sonar_console_lock();
    xil_printf("INIT: cycle=%s DMA=%s codec=%s codec_begin=%s motor_bus=%s Tic=%s step_mode=%s\r\n",
        startup_result(true,cycle_ok),startup_result(cycle_ok,dma_ok),startup_result(true,codec_ok),
        startup_result(codec_ok,codec_begin_ok),startup_result(true,motor_bus_ok),
        startup_result(motor_bus_ok,tic_ok),startup_result(tic_ok,mode_ok));
    xil_printf("INIT AUDIO: %s capabilities=0x%x; TCP 5003 configuration required before R.\r\n",
        audio_ok?"PASS":"UNAVAILABLE",(unsigned)sonar_chirp_capabilities());
    xil_printf("INIT WAV: %s max_samples=%u; RX remains 50 ms.\r\n",
        sonar_wav_player_available()?"READY":"UNAVAILABLE",(unsigned)sonar_tx_bram_capacity());
    sonar_console_unlock();
    while (ok && sonar_speaker_board_state()->state==SPEAKER_CONFIGURING) {
        sonar_speaker_board_poll((uint32_t)xTaskGetTickCount()); vTaskDelay(1);
    }
    const sonar_speaker_t *speaker=sonar_speaker_board_state();
    ok=ok && speaker!=NULL && speaker->state==SPEAKER_READY;
    taskENTER_CRITICAL(); ok=ok && !inhibited; taskEXIT_CRITICAL();
    if (!ok) {
        /* Record causes before shutdown can overwrite device status. */
        if (!motor_ready) { startup_motor_report(); }
        if (speaker!=NULL) {
            sonar_console_lock();
            xil_printf("INIT CODEC: state=%u fault=%u write_index=%u (OFF=0 CONFIGURING=1 READY=2 FAULT=3)\r\n",
                (unsigned)speaker->state,(unsigned)speaker->fault,(unsigned)speaker->index);
            sonar_console_unlock();
        }
        if (!motor_ready && codec_begin_ok) { message("INIT CODEC: configuration may be incomplete because motor initialization failed."); }
        (void)stop_outputs(NULL); cycle.state=CYCLE_FAULT;
        message("STAGE2 not ready: peripheral initialization or health check failed. Reset to retry.");
    } else {
        message("READY: codec writes, DMA setup and Tic communication checked; acoustic/motion tests require a run.");
        message("n: enter signed positions per 360 degrees then Enter; v: enter divisor; r: run one revolution; x: stop; s: status; ?: help");
        message("Example: n 30 gives 30 capture positions and exactly 800 Tic position units over 360 degrees. Sign selects direction.");
        message("TX: fixed 96 kHz, GENERATE or BRAM WAV; max 49 ms / BRAM capacity. RX: 50 ms, PCM 96 kHz.");
        message("Motor deadline 2000 ms from trigger; settle 2000 ms after movement.");
        sonar_console_lock();
        xil_printf("RX contract: %u words, %u bytes, %u us. Ethernet: " SONAR_NET_IP ":5001.\r\n",
            (unsigned)SONAR_STAGE2_WORDS,(unsigned)SONAR_STAGE2_BYTES,(unsigned)SONAR_STAGE2_RX_US);
        sonar_console_unlock();
    }
    sonar_number_t number={0}; uint8_t prompt=0;
    bool fault_reported=!ok;
    uint32_t last_motor=0;
    for (;;) {
        bool stop, fatal, overflow;
        sonar_rtos_heartbeat();
        taskENTER_CRITICAL(); stop=stop_requested; stop_requested=false;
        fatal=inhibited; overflow=input_overflow; input_overflow=false; taskEXIT_CRITICAL();
        if (stop) {
            (void)sonar_cycle_stop(&cycle,now_us()); prompt=0; number=(sonar_number_t){0};
            xQueueReset(keys); message("STOP requested; any in-flight capture will drain before restart.");
        }
        if (overflow) { message("Input overflow: input discarded and experiment stopped; re-enter settings."); }
        if (idle() && filling<0) { sonar_experiment_running(false); }
        sonar_control_poll(ok && !fatal && !stop && idle() && filling<0);
        uint8_t key;
        for (unsigned budget=0;budget<16U && xQueueReceive(keys,&key,0)==pdPASS;++budget) {
            if (key=='s') { report(); continue; }
            if (key=='?') { message("n <signed positions 1..800> Enter; v <1/2/4/8/16/32/64/128/256> Enter; r one 360-degree scan; x stop; s status"); continue; }
            if (prompt!=0) {
                /* Accept terminals that append Enter to the n/v command itself. */
                if ((key=='\r' || key=='\n') && number.length==0 && !number.invalid) { continue; }
                int32_t value=0;
                sonar_number_result_t result=sonar_number_feed(&number,key,&value);
                if (result!=NUMBER_WAIT) {
                    bool valid=result==NUMBER_OK;
                    if (prompt=='n' && valid && sonar_scan360_configure(&scan360,value)) {
                        requested_positions=(int32_t)value; message("Positions-per-revolution stored. Press R for one complete 360-degree scan; X stops.");
                    } else if (prompt=='v' && valid && value>0 && value<=256 && sonar_stepper_mode(&motor,(uint32_t)value)) {
                        message("Step divisor applied and read back from Tic.");
                    } else { message("Invalid or rejected value; use s to inspect settings."); }
                    prompt=0; number=(sonar_number_t){0}; report();
                }
                continue;
            }
            if ((key=='n' || key=='v') && idle()) {
                prompt=key; number=(sonar_number_t){0};
                message(key=='n'?"Enter signed positions per 360 degrees (1..800), then Enter:":"Enter step divisor, then Enter (1 = full steps):");
            } else if (key=='r') {
                if (!fatal && requested_positions!=0 && idle() && sonar_experiment_can_start()) {
                    const sonar_chirp_values_t *tv=sonar_experiment_values();
                    cycle.config.tx_timeout_us=tv->duration_us+SONAR_TX_START_MARGIN_US+SONAR_TX_TIMEOUT_MARGIN_US;
                    sonar_scan360_begin(&scan360);
                    if (sonar_cycle_start(&cycle,requested_positions,now_us())) {
                        sonar_experiment_running(true);
                        message("RUN: one 360-degree scan started; selected capture positions share exactly 800 Tic position units."); report();
                    } else { message("Cannot start: wait for STOPPED; faults require reset."); }
                }
                else if (!idle() && cycle.state!=CYCLE_FAULT) { message("Already running; X stops the current 360-degree scan."); }
                else { message("Cannot start: apply audio configuration on TCP 5003, enter n, and wait for STOPPED; faults require reset."); }
            }
        }
        uint32_t tick=(uint32_t)xTaskGetTickCount();
        if (motor_ready && tick-last_motor>=pdMS_TO_TICKS(20)) { sonar_stepper_poll(&motor); last_motor=tick; }
        tick=(uint32_t)xTaskGetTickCount(); /* I2C can yield while DMA completes. */
        bool captured=false;
        if (filling>=0 && mic.state==MIC_CAPTURING) {
            sonar_mic_event_t event;
            if (sonar_mic_zynq_take_event(&event)) { sonar_mic_event(&mic,&event,tick); }
            sonar_mic_poll(&mic,tick);
            if (mic.state==MIC_READY) { metadata.completion_us=now_us(); }
        }
        bool tx_fault=sonar_chirp_error();
        bool tx_done=sonar_chirp_done();
        /* Keep the RX slot owned until TX completion (measured or estimated). Stop drains
         * RX but discards that interrupted capture; earlier frames remain. */
        if (filling>=0 && mic.state==MIC_READY && tx_done && !tx_fault) {
            if (cycle.stopping) {
                taskENTER_CRITICAL(); sonar_frames.slots[filling].state=FRAME_FREE; taskEXIT_CRITICAL();
                message("Interrupted capture drained and discarded.");
            } else {
                taskENTER_CRITICAL(); bool published=sonar_frames_publish(&sonar_frames,(unsigned)filling,&metadata); taskEXIT_CRITICAL();
                if (!published) { fatal=true; }
                sonar_console_lock();
                xil_printf("CYCLE %u: RX complete; TX finished; %u bytes queued in DDR\r\n",
                    (unsigned)metadata.sequence,(unsigned)SONAR_STAGE2_BYTES);
                sonar_console_unlock();
            }
            (void)sonar_mic_release(&mic); filling=-1; captured=true;
        }
        sonar_cycle_state_t before=cycle.state;
        sonar_cycle_poll(&cycle,now_us(),captured,tx_done,motor.state==STEPPER_DONE,
            fatal || tx_fault || !ok || motor.state==STEPPER_FAULT || mic.state==MIC_FAULT);
        if (before==CYCLE_ACQUIRE && cycle.state!=CYCLE_ACQUIRE) {
            if (!sonar_speaker_board_enable(false)) { sonar_stage2_inhibit(); }
        }
        if (before==CYCLE_MOVING && cycle.state==CYCLE_SETTLING) {
            message("Motor completed; 2-second settling pause starts now.");
        }
        if (before==CYCLE_SETTLING && cycle.state==CYCLE_WAIT_BUFFER) {
            if (sonar_scan360_complete(&scan360)) {
                (void)sonar_cycle_stop(&cycle,now_us());
                sonar_console_lock();
                xil_printf("SCAN COMPLETE: %u capture positions, %u Tic units = 360 degrees; motor_pos=%d\r\n",
                    (unsigned)sonar_scan360_positions(&scan360),(unsigned)SONAR_SCAN360_REVOLUTION_STEPS,
                    (int)motor.status.position);
                sonar_console_unlock();
            } else {
                sonar_console_lock();
                xil_printf("CYCLE %u complete; next scan position follows automatically.\r\n",(unsigned)cycle.cycle);
                sonar_console_unlock();
            }
        }
        if (cycle.state==CYCLE_FAULT && !fault_reported) {
            if (mic.state==MIC_CAPTURING) { sonar_mic_cancel(&mic); }
            message("STAGE2 FAULT: outputs stopped; retained frames can still transfer. Reset required.");
            sonar_console_lock();
            xil_printf("Cycle cause=%s in %s; MIC fault=%s; motor cause=%s before-stop errors=0x%x valid=%u; after-stop errors=0x%x\r\n",
                cycle.fault_reason!=NULL?cycle.fault_reason:"INITIALIZATION",sonar_cycle_name(cycle.fault_state),
                sonar_mic_fault_name(mic.fault),motor.fault_reason!=NULL?motor.fault_reason:"none",
                (unsigned)motor.fault_status.errors,(unsigned)motor.fault_status_valid,(unsigned)motor.status.errors);
            sonar_console_unlock();
            fault_reported=true;
        }
        /* Sleep one tick, or less when a key or DMA completion notifies this task. */
        (void)ulTaskNotifyTake(pdTRUE,1U);
    }
}
bool sonar_stage2_create(void)
{
    if (!sonar_control_create()) { return false; }
    keys=xQueueCreate(64,1);
    if (keys==NULL || xTaskCreate(experiment_task,"experiment",2048,NULL,3,&experiment_handle)!=pdPASS) { return false; }
    return sys_thread_new("ethernet",sonar_network_task,NULL,2048,2)!=NULL;
}
