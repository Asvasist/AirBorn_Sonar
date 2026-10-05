`timescale 1ns / 1ps

// -----------------------------------------------------------------------------
// File: clk_divider_with_tick.v
//
// Purpose:
//   Generate a 2.4 MHz PDM microphone clock from a 125 MHz FPGA clock.
//
// Notes:
//   - sysclk/AXI remains at 125 MHz.
//   - Only clk_out is sent to the microphone.
//   - A phase accumulator is used because 125 MHz / 2.4 MHz is not an integer.
//   - tick_rising and tick_falling are one sysclk cycle wide.
// -----------------------------------------------------------------------------

module clk_divider_with_tick #(
    parameter integer INPUT_CLK_HZ  = 125_000_000,
    parameter integer OUTPUT_CLK_HZ = 2_400_000,
    parameter integer ACC_WIDTH     = 32
)(
    input  wire clk_in,
    input  wire rst,

    output wire clk_out,
    output reg  tick_rising,
    output reg  tick_falling
);

    reg [ACC_WIDTH-1:0] phase_accumulator;

    wire [ACC_WIDTH-1:0] phase_next;

    // Use 64-bit arithmetic so that 2^32 does not overflow.
    localparam [63:0] PHASE_MODULUS =
        (64'd1 << ACC_WIDTH);

    localparam [63:0] PHASE_INCREMENT_64 =
        ((PHASE_MODULUS * OUTPUT_CLK_HZ)
         + (INPUT_CLK_HZ / 2))
        / INPUT_CLK_HZ;

    localparam [ACC_WIDTH-1:0] PHASE_INCREMENT =
        PHASE_INCREMENT_64[ACC_WIDTH-1:0];

    assign phase_next =
        phase_accumulator + PHASE_INCREMENT;

    // The most significant accumulator bit is the microphone clock.
    assign clk_out =
        phase_accumulator[ACC_WIDTH-1];

    always @(posedge clk_in) begin
        if (rst) begin
            phase_accumulator <= {ACC_WIDTH{1'b0}};
            tick_rising       <= 1'b0;
            tick_falling      <= 1'b0;
        end
        else begin
            // Detect transitions that will occur during this update.
            tick_rising <=
                (~phase_accumulator[ACC_WIDTH-1])
                & phase_next[ACC_WIDTH-1];

            tick_falling <=
                phase_accumulator[ACC_WIDTH-1]
                & (~phase_next[ACC_WIDTH-1]);

            phase_accumulator <= phase_next;
        end
    end

endmodule