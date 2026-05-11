module led_waterfall (
    input  wire       clk,
    input  wire       rst_n,
    output reg  [7:0] led
);

    // 50MHz 时钟下，计数到 25_000_000 大约是 0.5 秒
    parameter CNT_MAX = 25_000_000;

    reg [24:0] cnt;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            cnt <= 25'd0;
            led <= 8'b0000_0001;
        end else begin
            if (cnt == CNT_MAX - 1) begin
                cnt <= 25'd0;

                // 流水灯左移
                if (led == 8'b1000_0000)
                    led <= 8'b0000_0001;
                else
                    led <= led << 1;

            end else begin
                cnt <= cnt + 1'b1;
            end
        end
    end

endmodule