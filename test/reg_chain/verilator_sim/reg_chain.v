module reg_chain (
    input wire clk,
    output wire [7:0] wire_A,
    output wire [7:0] wire_B,
    output reg [7:0] reg_A,
    output reg [7:0] reg_B,
    output wire [7:0] debug_A,
    output wire [7:0] debug_B
);
    reg [7:0] A;
    reg [7:0] B;

    assign wire_A = A;
    assign wire_B = B;
    assign debug_A = A;
    assign debug_B = B;

    always @(*) begin
        reg_A = A;
        reg_B = B;
    end

    initial begin
        A = 8'd0;
        B = 8'd0;
    end

    always @(posedge clk) begin
        A <= A + 8'd1;
        B <= A;
    end
endmodule
