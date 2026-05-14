module led_waterfall #(
    parameter [31:0] CNT_MAX = 32'd4
) (
    input  wire       clk,
    input  wire       rst_n,
    output reg  [7:0] led,
    output wire [24:0] debug_cnt,
    output reg        debug_wrap_pulse
);

    reg [24:0] cnt;

    assign debug_cnt = cnt;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            cnt <= 25'd0;
            led <= 8'b0000_0001;
            debug_wrap_pulse <= 1'b0;
        end else begin
            if ({7'd0, cnt} == CNT_MAX - 32'd1) begin
                cnt <= 25'd0;
                debug_wrap_pulse <= 1'b1;

                if (led == 8'b1000_0000)
                    led <= 8'b0000_0001;
                else
                    led <= led << 1;

            end else begin
                cnt <= cnt + 1'b1;
                debug_wrap_pulse <= 1'b0;
            end
        end
    end

endmodule
