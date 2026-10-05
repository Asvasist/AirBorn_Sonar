`timescale 1ns / 1ps

module clock_gen_audio
(
    input  wire clk125,     // ACTUAL input clock = 125 MHz
    input  wire reset,      // active-high reset

    output wire clk_audio,  // approximately 12.288 MHz
    output wire locked
);

    wire clkfb_mmcm;
    wire clkfb_buf;
    wire clkout_mmcm;


    // ========================================================
    // Input clock = 125 MHz
    //
    // Input period:
    //      1 / 125 MHz = 8.000 ns
    //
    // MMCM configuration:
    //
    // DIVCLK_DIVIDE       = 8
    // CLKFBOUT_MULT_F     = 39.125
    // CLKOUT0_DIVIDE_F    = 49.750
    //
    // VCO:
    //
    // 125 MHz * 39.125 / 8
    //      = 611.328125 MHz
    //
    // Audio MCLK:
    //
    // 611.328125 MHz / 49.750
    //      ≈ 12.2880025 MHz
    //
    // Error from ideal 12.288 MHz is extremely small
    // (~0.2 ppm).
    //
    // This gives:
    //
    // MCLK  ≈ 12.288 MHz
    // BCLK  ≈ 3.072 MHz   if divided by 4
    // LRCLK ≈ 48 kHz      if divided by 64 from BCLK
    // ========================================================

    MMCME2_BASE
    #(
        .BANDWIDTH("OPTIMIZED"),

        .CLKIN1_PERIOD(8.000),

        .DIVCLK_DIVIDE(8),

        .CLKFBOUT_MULT_F(39.125),

        .CLKOUT0_DIVIDE_F(49.750),

        .CLKOUT0_DUTY_CYCLE(0.500),

        .CLKOUT0_PHASE(0.000),

        .STARTUP_WAIT("FALSE")
    )
    audio_mmcm
    (
        .CLKIN1(clk125),

        .RST(reset),

        .PWRDWN(1'b0),

        .CLKFBIN(clkfb_buf),

        .CLKFBOUT(clkfb_mmcm),

        .CLKOUT0(clkout_mmcm),

        .LOCKED(locked)
    );


    // MMCM feedback global buffer
    BUFG feedback_clock_buffer
    (
        .I(clkfb_mmcm),
        .O(clkfb_buf)
    );


    // Audio master clock global buffer
    BUFG audio_clock_buffer
    (
        .I(clkout_mmcm),
        .O(clk_audio)
    );

endmodule