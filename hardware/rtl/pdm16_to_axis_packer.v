`timescale 1ns / 1ps

module pdm16_to_axis_packer #(
    parameter integer WORDS_TO_CAPTURE = 60000
)(
    input  wire        clk,
    input  wire        rst,
    input  wire        start,

    input  wire        tick_rising,
    input  wire        tick_falling,

    // JP1 = VDD group: DATA1...DATA8
    // Port name kept unchanged to avoid changing the block design.
    input  wire [7:0]  pdm_data_rising,

    // JP2 = GND group: DATA11...DATA18
    // Port name kept unchanged to avoid changing the block design.
    input  wire [7:0]  pdm_data_falling,

    output reg  [31:0] m_axis_tdata,
    output wire [3:0]  m_axis_tkeep,
    output reg         m_axis_tvalid,
    input  wire        m_axis_tready,
    output reg         m_axis_tlast,

    output reg         busy,
    output reg         done,
    output reg         overflow,

    output reg [31:0]  words_sent,
    output reg [31:0]  sample_count,
    output reg [31:0]  latest_word
);

    assign m_axis_tkeep = 4'b1111;

    reg start_d;

    wire start_rising  = start & ~start_d;
    wire axis_transfer = m_axis_tvalid && m_axis_tready;

    // ============================================================
    // MICROPHONE CONFIGURATION
    //
    // JP1 = VDD
    // DATA1...DATA8
    // Mic asserts/changes DATA on rising PDM clock edge.
    // FPGA captures stable DATA on falling tick.
    //
    // JP2 = GND
    // DATA11...DATA18
    // Mic asserts/changes DATA on falling PDM clock edge.
    // FPGA captures stable DATA on rising tick.
    // ============================================================

    // DATA11...DATA18 captured on rising tick.
    reg [7:0] jp2_sample;

    // One complete 16-channel PDM sample waiting to be packed.
    reg [15:0] first_sample;
    reg        have_first_sample;

    // Number of 32-bit AXI words prepared.
    reg [31:0] words_loaded;

    always @(posedge clk)
    begin
        if (rst)
        begin
            start_d           <= 1'b0;

            jp2_sample        <= 8'd0;

            first_sample      <= 16'd0;
            have_first_sample <= 1'b0;

            m_axis_tdata      <= 32'd0;
            m_axis_tvalid     <= 1'b0;
            m_axis_tlast      <= 1'b0;

            busy              <= 1'b0;
            done              <= 1'b0;
            overflow          <= 1'b0;

            words_sent        <= 32'd0;
            words_loaded      <= 32'd0;
            sample_count      <= 32'd0;
            latest_word       <= 32'd0;
        end
        else
        begin
            start_d <= start;

            // ----------------------------------------------------
            // DMA accepted current AXI word
            // ----------------------------------------------------
            if (axis_transfer)
            begin
                words_sent <= words_sent + 1'b1;

                if (m_axis_tlast)
                begin
                    busy <= 1'b0;
                    done <= 1'b1;
                end

                m_axis_tvalid <= 1'b0;
                m_axis_tlast  <= 1'b0;
            end

            // ----------------------------------------------------
            // Start new capture
            // ----------------------------------------------------
            if (start_rising && !busy && !m_axis_tvalid)
            begin
                busy              <= 1'b1;
                done              <= 1'b0;
                overflow          <= 1'b0;

                words_sent        <= 32'd0;
                words_loaded      <= 32'd0;
                sample_count      <= 32'd0;

                jp2_sample        <= 8'd0;
                first_sample      <= 16'd0;
                have_first_sample <= 1'b0;

                latest_word       <= 32'd0;
            end

            // ----------------------------------------------------
            // JP2 = GND
            //
            // DATA11...DATA18
            //
            // SELECT = GND:
            // microphone asserts DATA on falling PDM edge,
            // therefore capture stable data at rising tick.
            // ----------------------------------------------------
            if (busy && tick_rising)
            begin
                jp2_sample <= pdm_data_falling;
            end

            // ----------------------------------------------------
            // JP1 = VDD
            //
            // DATA1...DATA8
            //
            // SELECT = VDD:
            // microphone asserts DATA on rising PDM edge,
            // therefore capture stable data at falling tick.
            //
            // current_sample[7:0]
            //     = DATA1...DATA8
            //
            // current_sample[15:8]
            //     = DATA11...DATA18
            // ----------------------------------------------------
            if (busy &&
                tick_falling &&
                (words_loaded < WORDS_TO_CAPTURE))
            begin : CAPTURE_SAMPLE

                reg [15:0] current_sample;

                current_sample = {
                    jp2_sample,        // [15:8] DATA11...DATA18
                    pdm_data_rising    // [7:0]  DATA1...DATA8
                };

                sample_count <= sample_count + 1'b1;

                // ------------------------------------------------
                // First 16-channel PDM instant
                // ------------------------------------------------
                if (!have_first_sample)
                begin
                    first_sample      <= current_sample;
                    have_first_sample <= 1'b1;
                end

                // ------------------------------------------------
                // Second 16-channel PDM instant
                //
                // Pack two 16-bit samples into one 32-bit AXI word.
                // ------------------------------------------------
                else
                begin
                    // PDM cannot be paused.
                    // DMA must consume the previous word before
                    // another packed word is generated.
                    if (!m_axis_tvalid || m_axis_tready)
                    begin
                        m_axis_tdata <= {
                            current_sample,
                            first_sample
                        };

                        latest_word <= {
                            current_sample,
                            first_sample
                        };

                        m_axis_tvalid <= 1'b1;

                        if (words_loaded == WORDS_TO_CAPTURE - 1)
                            m_axis_tlast <= 1'b1;
                        else
                            m_axis_tlast <= 1'b0;

                        words_loaded <= words_loaded + 1'b1;

                        have_first_sample <= 1'b0;
                    end
                    else
                    begin
                        overflow <= 1'b1;
                    end
                end
            end
        end
    end

endmodule