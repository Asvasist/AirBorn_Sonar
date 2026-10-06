`timescale 1ns / 1ps

// -----------------------------------------------------------------------------
// File: top_pdm_axis_capture.v
//
// Purpose:
//   Capture raw one-bit PDM microphone data,
//   pack 32 PDM bits into one 32-bit AXI4-Stream word,
//   and send the words to AXI DMA S2MM.
//
// Sonar configuration:
//
//   PDM clock     = 2.4 MHz, 16 channels
//   Capture time  = 50 ms (120,000 PDM instants per channel)
//   AXI words     = 60,000 (two 16-channel instants per word)
//   DMA bytes     = 240,000 bytes
//
// start_capture should be triggered at the same time
// as the speaker/chirp start signal.
// -----------------------------------------------------------------------------

module top_pdm_axis_capture #(
    parameter integer INPUT_CLK_HZ     = 125_000_000,
    parameter integer PDM_CLK_HZ       = 2_400_000,

    // 60,000 words x 2 instants = 120,000 instants; 120,000 / 2.4 MHz = 50 ms
    parameter integer WORDS_TO_CAPTURE = 60_000
)(
    // -------------------------------------------------------------------------
    // System clock
    // -------------------------------------------------------------------------

    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 sysclk CLK" *)
    (* X_INTERFACE_PARAMETER =
       "XIL_INTERFACENAME sysclk, \
        ASSOCIATED_BUSIF m_axis, \
        ASSOCIATED_RESET rst, \
        FREQ_HZ 125000000" *)
    input wire sysclk,


    // -------------------------------------------------------------------------
    // Active-high reset
    // -------------------------------------------------------------------------

    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 rst RST" *)
    (* X_INTERFACE_PARAMETER =
       "XIL_INTERFACENAME rst, \
        POLARITY ACTIVE_HIGH" *)
    input wire rst,


    // -------------------------------------------------------------------------
    // Sonar capture trigger
    //
    // Connect this to the SAME GPIO trigger that starts
    // the speaker chirp.
    // -------------------------------------------------------------------------

    input wire start_capture,


    // -------------------------------------------------------------------------
    // PDM microphone interface
    // -------------------------------------------------------------------------

    output wire pdm_clk_out,

   input wire [7:0] pdm_data_sj1,
   input wire [7:0] pdm_data_sj2,


    // -------------------------------------------------------------------------
    // AXI4-Stream output
    // Connect directly to:
    //
    // AXI DMA
    // S_AXIS_S2MM
    // -------------------------------------------------------------------------

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TDATA" *)
    (* X_INTERFACE_PARAMETER =
       "XIL_INTERFACENAME m_axis, \
        TDATA_NUM_BYTES 4, \
        HAS_TKEEP 1, \
        HAS_TLAST 1" *)

    output wire [31:0] m_axis_tdata,


    (* X_INTERFACE_INFO =
       "xilinx.com:interface:axis:1.0 m_axis TKEEP" *)

    output wire [3:0] m_axis_tkeep,


    (* X_INTERFACE_INFO =
       "xilinx.com:interface:axis:1.0 m_axis TVALID" *)

    output wire m_axis_tvalid,


    (* X_INTERFACE_INFO =
       "xilinx.com:interface:axis:1.0 m_axis TREADY" *)

    input wire m_axis_tready,


    (* X_INTERFACE_INFO =
       "xilinx.com:interface:axis:1.0 m_axis TLAST" *)

    output wire m_axis_tlast,


    // -------------------------------------------------------------------------
    // Debug / status
    // -------------------------------------------------------------------------

    output wire capture_busy,

    output wire capture_done,

    output wire capture_overflow,

    output wire [31:0] words_sent_debug,

    output wire [5:0] bit_count_debug,

    output wire [31:0] latest_word_debug,
    output wire        m_axis_tready_debug
);

assign m_axis_tready_debug = m_axis_tready;

    // =========================================================================
    // PDM clock edge ticks
    // =========================================================================

    wire tick_rising;
    wire tick_falling;


    // =========================================================================
    // PDM CLOCK GENERATOR
    //
    // 125 MHz
    //    ↓
    // 2.4 MHz PDM clock
    // =========================================================================

    clk_divider_with_tick #(
        .INPUT_CLK_HZ  (INPUT_CLK_HZ),
        .OUTPUT_CLK_HZ (PDM_CLK_HZ)
    )
    u_clk_divider_with_tick
    (
        .clk_in       (sysclk),

        .rst          (rst),

        .clk_out      (pdm_clk_out),

        .tick_rising  (tick_rising),

        .tick_falling (tick_falling)
    );


    // =========================================================================
    // PDM → AXI STREAM PACKER
    //
    // Each rising PDM clock:
    //
    //      pdm_data_in
    //            ↓
    //          1 bit
    //
    // 32 bits collected:
    //
    //      [31:0] AXI word
    //
    // Final word:
    //
    //      TLAST = 1
    //
    // =========================================================================

pdm16_to_axis_packer #(
    .WORDS_TO_CAPTURE(WORDS_TO_CAPTURE)
)
u_pdm16_to_axis_packer
(
    .clk              (sysclk),
    .rst              (rst),
    .start            (start_capture),

    .tick_rising      (tick_rising),
    .tick_falling     (tick_falling),

    // SJ1 = GND
    .pdm_data_rising  (pdm_data_sj1),

    // SJ2 = VDD
    .pdm_data_falling (pdm_data_sj2),

    .m_axis_tdata     (m_axis_tdata),
    .m_axis_tkeep     (m_axis_tkeep),
    .m_axis_tvalid    (m_axis_tvalid),
    .m_axis_tready    (m_axis_tready),
    .m_axis_tlast     (m_axis_tlast),

    .busy             (capture_busy),
    .done             (capture_done),
    .overflow         (capture_overflow),

    .words_sent       (words_sent_debug),
    .sample_count     (),
    .latest_word      (latest_word_debug)
);


endmodule