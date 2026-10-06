`timescale 1ns / 1ps

// ============================================================================
// File: chirp_generator.v
//
// Runtime LFM chirp generator.
//
// IMPORTANT:
// Expensive chirp calculations are performed by the ARM/Vitis software.
//
// FPGA receives:
//   start_phase_inc : initial NCO phase increment
//   phase_inc_step  : amount added to phase increment every sample
//   total_samples   : exact number of 48-kHz samples to generate
//   amplitude       : 0..32767
//
// Per audio sample FPGA only needs:
//
//   phase           += phase_increment
//   phase_increment += phase_inc_step
//
// No divider is synthesized.
// ============================================================================

module chirp_generator
(
    input  wire               clk,
    input  wire               reset,

    input  wire               start,

    input  wire [31:0]        start_phase_inc,
    input  wire signed [31:0] phase_inc_step,
    input  wire [31:0]        total_samples,
    input  wire [15:0]        amplitude,

    input  wire               sample_request,

    output reg  [15:0]        sample_out,
    output reg                sample_valid,

    output reg                busy,
    output reg                done
);


    // ========================================================================
    // Configuration latched at START
    // ========================================================================

    reg [31:0] start_phase_inc_reg;

    reg signed [31:0] phase_inc_step_reg;

    reg [31:0] total_samples_reg;

    reg [15:0] amplitude_reg;


    // ========================================================================
    // NCO state
    // ========================================================================

    reg [31:0] phase_accumulator;

    reg signed [31:0] phase_increment;

    reg [31:0] sample_counter;


    // ========================================================================
    // Quarter-wave sine lookup
    //
    // No external .mem file.
    // ========================================================================

    function signed [15:0] quarter_sine;

        input [5:0] address;

        begin

            case (address)

                6'd0:  quarter_sine = 16'sd0;
                6'd1:  quarter_sine = 16'sd804;
                6'd2:  quarter_sine = 16'sd1608;
                6'd3:  quarter_sine = 16'sd2410;
                6'd4:  quarter_sine = 16'sd3212;
                6'd5:  quarter_sine = 16'sd4011;
                6'd6:  quarter_sine = 16'sd4808;
                6'd7:  quarter_sine = 16'sd5602;

                6'd8:  quarter_sine = 16'sd6393;
                6'd9:  quarter_sine = 16'sd7179;
                6'd10: quarter_sine = 16'sd7962;
                6'd11: quarter_sine = 16'sd8739;
                6'd12: quarter_sine = 16'sd9512;
                6'd13: quarter_sine = 16'sd10278;
                6'd14: quarter_sine = 16'sd11039;
                6'd15: quarter_sine = 16'sd11793;

                6'd16: quarter_sine = 16'sd12539;
                6'd17: quarter_sine = 16'sd13279;
                6'd18: quarter_sine = 16'sd14010;
                6'd19: quarter_sine = 16'sd14732;
                6'd20: quarter_sine = 16'sd15446;
                6'd21: quarter_sine = 16'sd16151;
                6'd22: quarter_sine = 16'sd16846;
                6'd23: quarter_sine = 16'sd17530;

                6'd24: quarter_sine = 16'sd18204;
                6'd25: quarter_sine = 16'sd18868;
                6'd26: quarter_sine = 16'sd19519;
                6'd27: quarter_sine = 16'sd20159;
                6'd28: quarter_sine = 16'sd20787;
                6'd29: quarter_sine = 16'sd21403;
                6'd30: quarter_sine = 16'sd22005;
                6'd31: quarter_sine = 16'sd22594;

                6'd32: quarter_sine = 16'sd23170;
                6'd33: quarter_sine = 16'sd23731;
                6'd34: quarter_sine = 16'sd24279;
                6'd35: quarter_sine = 16'sd24811;
                6'd36: quarter_sine = 16'sd25329;
                6'd37: quarter_sine = 16'sd25832;
                6'd38: quarter_sine = 16'sd26319;
                6'd39: quarter_sine = 16'sd26790;

                6'd40: quarter_sine = 16'sd27245;
                6'd41: quarter_sine = 16'sd27683;
                6'd42: quarter_sine = 16'sd28105;
                6'd43: quarter_sine = 16'sd28510;
                6'd44: quarter_sine = 16'sd28898;
                6'd45: quarter_sine = 16'sd29268;
                6'd46: quarter_sine = 16'sd29621;
                6'd47: quarter_sine = 16'sd29956;

                6'd48: quarter_sine = 16'sd30273;
                6'd49: quarter_sine = 16'sd30571;
                6'd50: quarter_sine = 16'sd30852;
                6'd51: quarter_sine = 16'sd31113;
                6'd52: quarter_sine = 16'sd31356;
                6'd53: quarter_sine = 16'sd31580;
                6'd54: quarter_sine = 16'sd31785;
                6'd55: quarter_sine = 16'sd31971;

                6'd56: quarter_sine = 16'sd32137;
                6'd57: quarter_sine = 16'sd32285;
                6'd58: quarter_sine = 16'sd32412;
                6'd59: quarter_sine = 16'sd32521;
                6'd60: quarter_sine = 16'sd32609;
                6'd61: quarter_sine = 16'sd32678;
                6'd62: quarter_sine = 16'sd32728;
                6'd63: quarter_sine = 16'sd32757;

                default:
                    quarter_sine = 16'sd0;

            endcase

        end

    endfunction


    // ========================================================================
    // Full sine from phase accumulator
    // ========================================================================

    function signed [15:0] sine_from_phase;

        input [31:0] phase;

        reg [1:0] quadrant;
        reg [5:0] index;

        reg signed [15:0] value;

        begin

            quadrant =
                phase[31:30];

            index =
                phase[29:24];


            case (quadrant)

                2'b00:
                    value =
                        quarter_sine(index);

                2'b01:
                    value =
                        quarter_sine(
                            6'd63 - index
                        );

                2'b10:
                    value =
                        -quarter_sine(index);

                default:
                    value =
                        -quarter_sine(
                            6'd63 - index
                        );

            endcase


            sine_from_phase =
                value;

        end

    endfunction


    // ========================================================================
    // Amplitude scaling
    // ========================================================================

    reg signed [15:0] sine_value;

    reg signed [32:0] multiplied_sample;


    // ========================================================================
    // Main
    // ========================================================================

    always @(posedge clk)
    begin

        if (reset)
        begin

            start_phase_inc_reg <=
                32'd0;

            phase_inc_step_reg <=
                32'sd0;

            total_samples_reg <=
                32'd0;

            amplitude_reg <=
                16'd0;


            phase_accumulator <=
                32'd0;

            phase_increment <=
                32'sd0;

            sample_counter <=
                32'd0;


            sample_out <=
                16'd0;

            sample_valid <=
                1'b0;


            busy <=
                1'b0;

            done <=
                1'b0;

        end

        else
        begin

            // one-cycle pulses

            sample_valid <=
                1'b0;

            done <=
                1'b0;


            // ================================================================
            // Start new chirp
            // ================================================================

            if (
                start &&
                !busy
            )
            begin

                // Capture configuration.

                start_phase_inc_reg <=
                    start_phase_inc;

                phase_inc_step_reg <=
                    phase_inc_step;

                total_samples_reg <=
                    total_samples;

                amplitude_reg <=
                    amplitude;


                // Initialize NCO.

                phase_accumulator <=
                    32'd0;

                phase_increment <=
                    $signed(start_phase_inc);

                sample_counter <=
                    32'd0;


                // Do not start an empty chirp.

                if (total_samples != 0)
                begin

                    busy <=
                        1'b1;

                end

                else
                begin

                    busy <=
                        1'b0;

                    done <=
                        1'b1;

                end

            end


            // ================================================================
            // Generate one requested sample
            // ================================================================

            else if (
                busy &&
                sample_request
            )
            begin

                sine_value =
                    sine_from_phase(
                        phase_accumulator
                    );


                multiplied_sample =
                    sine_value *
                    $signed({
                        1'b0,
                        amplitude_reg
                    });


                sample_out <=
                    multiplied_sample >>> 15;


                sample_valid <=
                    1'b1;


                // NCO update

                phase_accumulator <=
                    phase_accumulator +
                    phase_increment;


                phase_increment <=
                    phase_increment +
                    phase_inc_step_reg;


                // ------------------------------------------------------------
                // Last sample
                // ------------------------------------------------------------

                if (
                    sample_counter + 1 >=
                    total_samples_reg
                )
                begin

                    busy <=
                        1'b0;

                    done <=
                        1'b1;

                    sample_counter <=
                        32'd0;

                end

                else
                begin

                    sample_counter <=
                        sample_counter + 1'b1;

                end

            end

        end

    end


endmodule