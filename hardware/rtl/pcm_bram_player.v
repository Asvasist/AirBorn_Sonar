`timescale 1ns / 1ps

module pcm_bram_player #(
    parameter ADDR_WIDTH = 32       // 2048 words
)(
    input  wire                  clk,
    input  wire                  reset,

    input  wire                  start,
    input  wire [31:0]           total_samples,

    // Same handshake style as your chirp_generator
    input  wire                  sample_request,

    output reg  [15:0]           sample_out,
    output reg                   sample_valid,
    output reg                   busy,
    output reg                   done,

    // Block RAM Port B
    output reg                   bram_en,
    output reg [ADDR_WIDTH-1:0]  bram_addr,
    input  wire [31:0]           bram_dout
);

    reg [31:0] sample_count;
    reg [31:0] total_samples_reg;
    reg        read_pending;

    always @(posedge clk)
    begin
        if (reset)
        begin
            sample_out       <= 16'd0;
            sample_valid     <= 1'b0;
            busy             <= 1'b0;
            done             <= 1'b0;

            bram_en          <= 1'b0;
           bram_addr <= sample_count << 2;

            sample_count     <= 32'd0;
            total_samples_reg <= 32'd0;
            read_pending     <= 1'b0;
        end
        else
        begin
            sample_valid <= 1'b0;
            done         <= 1'b0;
            bram_en      <= 1'b0;

            // Start playback
            if (start && !busy)
            begin
                sample_count      <= 32'd0;
                total_samples_reg <= total_samples;
                read_pending      <= 1'b0;

                if (total_samples != 0)
                    busy <= 1'b1;
                else
                    done <= 1'b1;
            end

            // BRAM output is available one clock after address request
            else if (read_pending)
            begin
                sample_out   <= bram_dout[15:0];
                sample_valid <= 1'b1;
                read_pending <= 1'b0;

                if (sample_count + 1 >= total_samples_reg)
                begin
                    busy         <= 1'b0;
                    done         <= 1'b1;
                    sample_count <= 32'd0;
                end
                else
                begin
                    sample_count <= sample_count + 1'b1;
                end
            end

            // Request next PCM word
            else if (busy && sample_request)
            begin
                bram_addr <= sample_count << 2;
                bram_en      <= 1'b1;
                read_pending <= 1'b1;
            end
        end
    end

endmodule