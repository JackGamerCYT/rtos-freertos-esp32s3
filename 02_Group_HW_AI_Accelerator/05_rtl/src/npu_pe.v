// PE của systolic array (output-stationary).
// mode = 0: INT8 x INT8 dùng bộ nhân. mode = 1: trọng số PoT 4 bit {sign, e[2:0]} -> dịch trái a<<<e.
// PE_MODE (tham số tĩnh): 0 = dual (chọn theo thanh ghi mode lúc chạy), 1 = chỉ INT8, 2 = chỉ PoT.
module npu_pe #(
    parameter PE_MODE = 0
) (
    input  wire               clk,
    input  wire               rst,
    input  wire               clr,
    input  wire               en,
    input  wire               mode,
    input  wire [7:0]         a_in,
    input  wire [7:0]         w_in,
    output reg  [7:0]         a_out,
    output reg  [7:0]         w_out,
    output reg  signed [31:0] acc
);
    wire signed [7:0]  a_s   = a_in;
    wire signed [7:0]  w_s   = w_in;
    wire signed [15:0] p_int = a_s * w_s;

    wire [2:0]         e     = w_in[2:0];
    wire signed [15:0] a_ext = {{8{a_in[7]}}, a_in};
    wire signed [15:0] sh    = a_ext <<< e;             // |a| <= 127, e <= 6 -> vừa 16 bit
    wire signed [15:0] p_pot = (e == 3'd7) ? 16'sd0 : (w_in[3] ? -sh : sh);

    wire use_pot = (PE_MODE == 2) ? 1'b1 : (PE_MODE == 1) ? 1'b0 : mode;
    wire signed [15:0] prod = use_pot ? p_pot : p_int;

    always @(posedge clk) begin
        if (rst) begin
            a_out <= 8'd0;
            w_out <= 8'd0;
            acc   <= 32'sd0;
        end else begin
            a_out <= a_in;
            w_out <= w_in;
            if (clr)
                acc <= 32'sd0;
            else if (en)
                acc <= acc + {{16{prod[15]}}, prod};
        end
    end
endmodule
