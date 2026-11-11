// Đường trễ D nhịp (dùng cho skew dữ liệu vào systolic array)
module npu_delay #(
    parameter W = 8,
    parameter D = 1
) (
    input  wire         clk,
    input  wire         rst,
    input  wire [W-1:0] d,
    output wire [W-1:0] q
);
    reg [W-1:0] sr [0:D-1];
    integer i;
    always @(posedge clk) begin
        if (rst) begin
            for (i = 0; i < D; i = i + 1) sr[i] <= {W{1'b0}};
        end else begin
            sr[0] <= d;
            for (i = 1; i < D; i = i + 1) sr[i] <= sr[i-1];
        end
    end
    assign q = sr[D-1];
endmodule
