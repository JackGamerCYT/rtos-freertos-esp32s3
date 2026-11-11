// Requantization 2 tầng pipeline: y = clamp(((acc+bias)*M + 2^(S-1)) >>> S), có ReLU / raw bypass.
module npu_requant (
    input  wire               clk,
    input  wire               in_valid,
    input  wire signed [31:0] acc,
    input  wire signed [31:0] bias,
    input  wire [15:0]        mult,
    input  wire [5:0]         shift,
    input  wire               relu,
    input  wire               raw,
    input  wire [12:0]        in_addr,
    output reg                out_valid,
    output reg  [31:0]        out_data,
    output reg  [12:0]        out_addr
);
    reg               v1;
    reg signed [31:0] sum1;
    reg [12:0]        addr1;
    reg signed [63:0] prod1;

    always @(posedge clk) begin
        // tầng 1: cộng bias, nhân
        v1    <= in_valid;
        addr1 <= in_addr;
        sum1  <= acc + bias;
        prod1 <= (acc + bias) * $signed({1'b0, mult});
    end

    wire signed [63:0] rnd     = (shift == 0) ? 64'sd0 : (64'sd1 <<< (shift - 1));
    wire signed [63:0] shifted = (prod1 + rnd) >>> shift;
    wire signed [63:0] lo      = relu ? 64'sd0 : -64'sd128;
    wire signed [63:0] clamped = (shifted < lo) ? lo : (shifted > 64'sd127) ? 64'sd127 : shifted;

    always @(posedge clk) begin
        // tầng 2: làm tròn, dịch, kẹp
        out_valid <= v1;
        out_addr  <= addr1;
        out_data  <= raw ? sum1 : clamped[31:0];
    end
endmodule
