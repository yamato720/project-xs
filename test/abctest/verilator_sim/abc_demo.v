module abc_demo (
    input wire clk,
    input wire rst_n,
    input wire [63:0] source_out,
    output wire [63:0] wire_A,
    output wire [63:0] wire_B,
    output wire [63:0] wire_C,
    output reg [63:0] reg_A,
    output reg [63:0] reg_B,
    output reg [63:0] reg_C
);
    reg [63:0] b_captured;
    reg [63:0] b_out;
    reg [63:0] c_captured;
    reg [63:0] c_out;
    reg [0:0] b_phase;
    reg [1:0] c_phase;
    reg c_align_remaining;

    reg [63:0] next_a_out;
    reg [63:0] next_b_captured;
    reg [63:0] next_b_out;
    reg [63:0] next_c_captured;
    reg [63:0] next_c_out;
    reg [0:0] next_b_phase;
    reg [1:0] next_c_phase;
    reg next_c_align_remaining;

    always @(*) begin
        next_a_out = source_out;
        next_b_captured = b_captured;
        next_b_out = b_out;
        if (b_phase == 1'b0) begin
            next_b_captured = next_a_out;
        end else begin
            next_b_out = b_captured + 64'd4;
        end

        next_c_captured = c_captured;
        next_c_out = c_out;
        if (!c_align_remaining) begin
            if (c_phase == 2'd0) begin
                next_c_captured = next_b_out;
            end else if (c_phase == 2'd1) begin
                next_c_out = c_captured * 64'd3;
            end
        end

        next_b_phase = b_phase + 1'b1;
        next_c_align_remaining = c_align_remaining;
        next_c_phase = c_phase;
        if (c_align_remaining) begin
            next_c_align_remaining = 1'b0;
        end else if (c_phase == 2'd2) begin
            next_c_phase = 2'd0;
        end else begin
            next_c_phase = c_phase + 2'd1;
        end
    end

    assign wire_A = source_out;
    assign wire_B = b_out;
    assign wire_C = c_out;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            b_captured <= 64'd0;
            b_out <= 64'd0;
            c_captured <= 64'd0;
            c_out <= 64'd0;
            b_phase <= 1'b0;
            c_phase <= 2'd0;
            c_align_remaining <= 1'b1;
            reg_A <= 64'd0;
            reg_B <= 64'd0;
            reg_C <= 64'd0;
        end else begin
            b_captured <= next_b_captured;
            b_out <= next_b_out;
            c_captured <= next_c_captured;
            c_out <= next_c_out;
            b_phase <= next_b_phase;
            c_phase <= next_c_phase;
            c_align_remaining <= next_c_align_remaining;
            reg_A <= next_a_out;
            reg_B <= next_b_out;
            reg_C <= next_c_out;
        end
    end
endmodule
