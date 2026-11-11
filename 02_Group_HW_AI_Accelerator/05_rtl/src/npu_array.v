// Mảng ROWS x COLS PE. Activation chảy từ trái sang phải, trọng số chảy từ trên xuống.
module npu_array #(
    parameter ROWS    = 8,
    parameter COLS    = 8,
    parameter PE_MODE = 0
) (
    input  wire                      clk,
    input  wire                      rst,
    input  wire                      clr,
    input  wire                      en,
    input  wire                      mode,
    input  wire [ROWS*8-1:0]         a_left,
    input  wire [COLS*8-1:0]         w_top,
    output wire [ROWS*COLS*32-1:0]   acc_flat
);
    // ah: dây ngang, (ROWS) x (COLS+1); wv: dây dọc, (ROWS+1) x COLS
    wire [ROWS*(COLS+1)*8-1:0] ah;
    wire [(ROWS+1)*COLS*8-1:0] wv;

    genvar r, c;
    generate
        for (r = 0; r < ROWS; r = r + 1) begin : g_left
            assign ah[(r*(COLS+1))*8 +: 8] = a_left[r*8 +: 8];
        end
        for (c = 0; c < COLS; c = c + 1) begin : g_top
            assign wv[c*8 +: 8] = w_top[c*8 +: 8];
        end
        for (r = 0; r < ROWS; r = r + 1) begin : g_r
            for (c = 0; c < COLS; c = c + 1) begin : g_c
                npu_pe #(.PE_MODE(PE_MODE)) u_pe (
                    .clk  (clk),
                    .rst  (rst),
                    .clr  (clr),
                    .en   (en),
                    .mode (mode),
                    .a_in (ah[(r*(COLS+1)+c)*8 +: 8]),
                    .w_in (wv[(r*COLS+c)*8 +: 8]),
                    .a_out(ah[(r*(COLS+1)+c+1)*8 +: 8]),
                    .w_out(wv[((r+1)*COLS+c)*8 +: 8]),
                    .acc  (acc_flat[(r*COLS+c)*32 +: 32])
                );
            end
        end
    endgenerate
endmodule
