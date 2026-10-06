`timescale 1ns / 1ps

// ============================================================================
// File: ssm2603_i2s_chirp_tx.v
//
// Purpose:
//   Runtime-configurable LFM chirp playback through the SSM2603 codec.
//
//   No .mem waveform file is used.
//
//   Vitis will eventually configure:
//       chirp_start_hz
//       chirp_stop_hz
//       chirp_duration_us
//       chirp_amplitude
//

//
// Audio format:
//   16-bit signed PCM
//   I2S
//   32 BCLK periods/channel
//   64 BCLK periods/frame
//   Mono duplicated to left and right
// ============================================================================

module ssm2603_i2s_chirp_tx (

    input  wire        mclk,
    input  wire        reset,

    input  wire        codec_ready,
    input  wire        codec_error,

    input  wire        start_async,
    input  wire        playback_mode,      // 0 = generated chirp, 1 = BRAM PCM
input  wire [15:0] pcm_sample,
input  wire        pcm_sample_valid,
input  wire        pcm_busy,
input  wire        pcm_done,

output wire        pcm_start,
output wire        pcm_sample_request,


    // ========================================================================
    // Runtime chirp configuration
    // ========================================================================

    input wire [31:0] chirp_start_phase_inc,
input wire signed [31:0] chirp_phase_inc_step,
input wire [31:0] chirp_total_samples,
input wire [15:0] chirp_amplitude,
    // ========================================================================
    // SSM2603 I2S outputs
    // ========================================================================
    

    output reg         ac_bclk,
    output reg         ac_pblrc,
    output reg         ac_pbdat,

    output wire        ac_muten,

    output reg         tx_busy,
    output reg         tx_done
);



    


    // ========================================================================
    // RESET CLOCK DOMAIN CROSSING
    // ========================================================================

    (* ASYNC_REG = "TRUE" *)
    reg [1:0] reset_sync = 2'b11;

    wire reset_mclk;

    assign reset_mclk = reset_sync[1];


    always @(posedge mclk or posedge reset)
    begin

        if (reset)
        begin
            reset_sync <= 2'b11;
        end

        else
        begin
            reset_sync <= {
                reset_sync[0],
                1'b0
            };
        end

    end


    // ========================================================================
    // START CLOCK DOMAIN CROSSING
    // ========================================================================

    (* ASYNC_REG = "TRUE" *)
    reg start_sync_1;

    (* ASYNC_REG = "TRUE", mark_debug = "true" *)
    reg start_sync_2;

    (* mark_debug = "true" *)
    reg start_sync_2_d;

    (* mark_debug = "true" *)
    reg pending_start;


    wire start_rise;

    assign start_rise =
        start_sync_2 &&
        !start_sync_2_d;


    // ========================================================================
    // CODEC STATUS CLOCK DOMAIN CROSSING
    // ========================================================================

    (* ASYNC_REG = "TRUE" *)
    reg codec_ready_sync_1;

    (* ASYNC_REG = "TRUE" *)
    reg codec_ready_sync_2;

    (* ASYNC_REG = "TRUE" *)
    reg codec_error_sync_1;

    (* ASYNC_REG = "TRUE" *)
    reg codec_error_sync_2;


    // ========================================================================
    // HARDWARE MUTE
    // ========================================================================

    assign ac_muten =
        codec_ready_sync_2 &&
        !codec_error_sync_2;


    // ========================================================================
    // CHIRP GENERATOR CONNECTION
    // ========================================================================

    reg chirp_start;

    reg chirp_sample_request;


    wire [15:0] chirp_sample;

    wire chirp_sample_valid;

    wire chirp_busy;

    wire chirp_done;
    
wire selected_busy =
    playback_mode ? pcm_busy : chirp_busy;

wire selected_done =
    playback_mode ? pcm_done : chirp_done;
assign pcm_start =
    chirp_start && playback_mode;

assign pcm_sample_request =
    chirp_sample_request && playback_mode;

chirp_generator chirp_generator_inst
(
    .clk(
        mclk
    ),

    .reset(
        reset_mclk
    ),

    .start(chirp_start && !playback_mode),

    .start_phase_inc(
        chirp_start_phase_inc
    ),

    .phase_inc_step(
        chirp_phase_inc_step
    ),

    .total_samples(
        chirp_total_samples
    ),

    .amplitude(
        chirp_amplitude
    ),

    .sample_request(
        chirp_sample_request
    ),

    .sample_out(
        chirp_sample
    ),

    .sample_valid(
        chirp_sample_valid
    ),

    .busy(
        chirp_busy
    ),

    .done(
        chirp_done
    )
);
    // ========================================================================
    // MCLK -> BCLK DIVIDER
    //
    // MCLK = 12.288 MHz
    //
    // ac_bclk toggles every two MCLK clocks.
    //
    // Therefore:
    //
    // BCLK = 12.288 MHz / 4
    //      = 3.072 MHz
    //
    // 64 BCLK periods/frame:
    //
    // LRCLK = 3.072 MHz / 64
    //        = 48 kHz
    // ========================================================================



    // ========================================================================
    // I2S STATE
    // ========================================================================

    (* mark_debug = "true" *)
    reg [5:0] slot_bit;

    (* mark_debug = "true" *)
    reg [15:0] current_sample;


    // ========================================================================
    // CHIRP SAMPLE HANDSHAKE STATE
    //
    // We request the next sample one frame before it is needed.
    // ========================================================================

    reg waiting_for_sample;

    reg chirp_started;


    // ========================================================================
    // I2S DATA BIT FUNCTION
    // ========================================================================

    function automatic i2s_data_bit;

        input [5:0] bit_number;

        input [15:0] sample_word;

        integer sample_bit;

        begin

            i2s_data_bit =
                1'b0;


            // ----------------------------------------------------------------
            // LEFT CHANNEL
            //
            // bit 0  = I2S one-bit delay
            // bit 1  = PCM bit 15
            // ...
            // bit 16 = PCM bit 0
            // ----------------------------------------------------------------

            if (
                (bit_number >= 6'd1) &&
                (bit_number <= 6'd16)
            )
            begin

                sample_bit =
                    16 - bit_number;

                i2s_data_bit =
                    sample_word[
                        sample_bit
                    ];

            end


            // ----------------------------------------------------------------
            // RIGHT CHANNEL
            //
            // bit 32 = I2S one-bit delay
            // bit 33 = PCM bit 15
            // ...
            // bit 48 = PCM bit 0
            // ----------------------------------------------------------------

            else if (
                (bit_number >= 6'd33) &&
                (bit_number <= 6'd48)
            )
            begin

                sample_bit =
                    48 - bit_number;

                i2s_data_bit =
                    sample_word[
                        sample_bit
                    ];

            end

        end

    endfunction


    // ========================================================================
    // PREPARE NEXT I2S BIT
    //
    // DATA and LRCLK are changed on the falling BCLK edge.
    // ========================================================================

    task automatic prepare_i2s_bit;

        input [5:0] bit_number;

        input [15:0] sample_word;

        begin

            // LEFT = 0
            // RIGHT = 1

            ac_pblrc <=
                (bit_number >= 6'd32);


            ac_pbdat <=
                i2s_data_bit(
                    bit_number,
                    sample_word
                );

        end

    endtask


    // ========================================================================
    // MAIN AUDIO TRANSMITTER
    // ========================================================================

    always @(posedge mclk)
    begin


        // ====================================================================
        // RESET
        // ====================================================================

        if (reset_mclk)
        begin


            // ----------------------------------------------------------------
            // Start synchronizer
            // ----------------------------------------------------------------

            start_sync_1 <=
                1'b0;

            start_sync_2 <=
                1'b0;

            start_sync_2_d <=
                1'b0;

            pending_start <=
                1'b0;


            // ----------------------------------------------------------------
            // Codec synchronizer
            // ----------------------------------------------------------------

            codec_ready_sync_1 <=
                1'b0;

            codec_ready_sync_2 <=
                1'b0;

            codec_error_sync_1 <=
                1'b1;

            codec_error_sync_2 <=
                1'b1;


            // ----------------------------------------------------------------
            // Chirp generator controls
            // ----------------------------------------------------------------

            chirp_start <=
                1'b0;

            chirp_sample_request <=
                1'b0;

            waiting_for_sample <=
                1'b0;

            chirp_started <=
                1'b0;


            // ----------------------------------------------------------------
            // Clock generation
            // ----------------------------------------------------------------

           

            ac_bclk <=
                1'b0;


            // ----------------------------------------------------------------
            // I2S
            // ----------------------------------------------------------------

            ac_pblrc <=
                1'b0;

            ac_pbdat <=
                1'b0;

            slot_bit <=
                6'd0;

            current_sample <=
                16'd0;


            // ----------------------------------------------------------------
            // Status
            // ----------------------------------------------------------------

            tx_busy <=
                1'b0;

            tx_done <=
                1'b0;

        end


        // ====================================================================
        // NORMAL OPERATION
        // ====================================================================

        else
        begin


            // =================================================================
            // Synchronize asynchronous inputs
            // =================================================================

            start_sync_1 <=
                start_async;

            start_sync_2 <=
                start_sync_1;

            start_sync_2_d <=
                start_sync_2;


            codec_ready_sync_1 <=
                codec_ready;

            codec_ready_sync_2 <=
                codec_ready_sync_1;


            codec_error_sync_1 <=
                codec_error;

            codec_error_sync_2 <=
                codec_error_sync_1;


            // =================================================================
            // Default one-cycle signals
            // =================================================================

            chirp_start <=
                1'b0;

            chirp_sample_request <=
                1'b0;

            tx_done <=
                1'b0;


            // =================================================================
            // Capture start request
            // =================================================================

            if (
                start_rise &&
                !tx_busy
            )
            begin

                pending_start <=
                    1'b1;

            end


            // =================================================================
            // Receive generated sample
            // =================================================================

           if ((!playback_mode && chirp_sample_valid) ||
    ( playback_mode && pcm_sample_valid))
begin
    current_sample <= playback_mode ? pcm_sample : chirp_sample;
    waiting_for_sample <= 1'b0;
end


            // =================================================================
            // CHIRP COMPLETE
            // =================================================================

            if (
                tx_busy &&
                selected_done
            )
            begin

                // The generator has produced its final sample.
                //
                // Do NOT immediately clear tx_busy here because that final
                // sample still needs to complete its I2S frame.

                chirp_started <=
                    1'b0;

            end


            // =================================================================
            // MCLK DIVIDER
            // =================================================================

            


                // =============================================================
                // BCLK RISING EDGE
                // =============================================================

                if (!ac_bclk)
                begin

                    ac_bclk <=
                        1'b1;

                end


                // =============================================================
                // BCLK FALLING EDGE
                // =============================================================

                else
                begin

                    ac_bclk <=
                        1'b0;


                    // =========================================================
                    // END OF 64-BIT FRAME
                    // =========================================================

                    if (
                        slot_bit ==
                        6'd63
                    )
                    begin

                        slot_bit <=
                            6'd0;


                        // =====================================================
                        // START NEW TRANSMISSION
                        // =====================================================

                        if (
                            !tx_busy &&
                            pending_start &&
                            codec_ready_sync_2 &&
                            !codec_error_sync_2
                        )
                        begin

                            pending_start <=
                                1'b0;

                            tx_busy <=
                                1'b1;

                            chirp_started <=
                                1'b1;


                            // Start chirp generator

                            chirp_start <=
                                1'b1;


                            // Output zero until first generated sample arrives.

                            current_sample <=
                                16'd0;

                            prepare_i2s_bit(
                                6'd0,
                                16'd0
                            );

                        end


                        // =====================================================
                        // TRANSMITTING
                        // =====================================================

                        else if (tx_busy)
                        begin


                            // -------------------------------------------------
                            // Generator has finished.
                            //
                            // chirp_busy falls after final requested sample.
                            // waiting_for_sample must also be clear, meaning
                            // final sample has arrived.
                            // -------------------------------------------------

                            if (
                                !selected_busy &&
                                !chirp_started &&
                                !waiting_for_sample
                            )
                            begin

                                tx_busy <=
                                    1'b0;

                                tx_done <=
                                    1'b1;

                                current_sample <=
                                    16'd0;

                                prepare_i2s_bit(
                                    6'd0,
                                    16'd0
                                );

                            end


                            // -------------------------------------------------
                            // Continue playback
                            // -------------------------------------------------

                            else
                            begin

                                // Request the next sample once per frame.

                                if (
                                    selected_busy &&
                                    !waiting_for_sample
                                )
                                begin

                                    chirp_sample_request <=
                                        1'b1;

                                    waiting_for_sample <=
                                        1'b1;

                                end


                                prepare_i2s_bit(
                                    6'd0,
                                    current_sample
                                );

                            end

                        end


                        // =====================================================
                        // IDLE
                        // =====================================================

                        else
                        begin

                            current_sample <=
                                16'd0;

                            prepare_i2s_bit(
                                6'd0,
                                16'd0
                            );

                        end

                    end


                    // =========================================================
                    // CONTINUE CURRENT FRAME
                    // =========================================================

                    else
                    begin

                        slot_bit <=
                            slot_bit + 1'b1;

                        prepare_i2s_bit(
                            slot_bit + 1'b1,
                            current_sample
                        );

                    end

                end

            end

        end

    


endmodule