// NPU core: GEMM tile ROWS x COLS + bias + requant + ReLU, buffer on-chip, bus host đơn giản.
//
// Bản đồ địa chỉ (byte, 20 bit) - xem 04_hw_spec/architecture.md:
//   0x00000 CSR            0x01000 BIAS[n]   (int32, 64 phần tử)
//   0x40000 A  bank*2048+addr (8 bit)        0x80000 W bank*2048+addr (8 bit)
//   0xC0000 OUT[m*N+n] (int32, chỉ đọc)
// CSR:
//   0x00 CTRL   [0]=start (ghi 1)           0x04 STATUS [0]=busy [1]=done
//   0x08 M      0x0C N      0x10 K
//   0x14 CFG    [0]=mode(0 INT8,1 POT) [1]=relu [2]=raw
//   0x18 MULT   0x1C SHIFT  0x20 CYCLES (đếm chu kỳ lần chạy gần nhất)
//   0x24 HWINFO {8'hA1, PE_MODE[3:0], 4'h0, ROWS[7:0], COLS[7:0]}
module npu_core #(
    parameter ROWS    = 8,
    parameter COLS    = 8,
    parameter PE_MODE = 0,
    parameter A_AW    = 11,     // 2048 phần tử / bank
    parameter W_AW    = 11,
    parameter O_AW    = 13      // 8192 kết quả
) (
    input  wire        clk,
    input  wire        rst,
    // bus host (từ AXI-Lite wrapper)
    input  wire        bus_we,
    input  wire        bus_re,
    input  wire [19:0] bus_addr,
    input  wire [31:0] bus_wdata,
    output reg  [31:0] bus_rdata,
    output wire        irq_done
);
    // ---------------- CSR ----------------
    reg [15:0] r_m, r_n, r_k;
    reg        r_mode, r_relu, r_raw;
    reg [15:0] r_mult;
    reg [5:0]  r_shift;
    reg [31:0] r_cycles;
    reg        busy, done;
    wire       start_pulse;

    wire [17:0] word   = bus_addr[19:2];
    wire [1:0]  region = bus_addr[19:18];
    wire        is_csr  = (region == 2'b00) && !bus_addr[12];
    wire        is_bias = (region == 2'b00) &&  bus_addr[12];
    wire        is_a    = (region == 2'b01);
    wire        is_w    = (region == 2'b10);
    wire        is_out  = (region == 2'b11);

    assign start_pulse = bus_we && is_csr && (bus_addr[7:0] == 8'h00) && bus_wdata[0] && !busy;
    assign irq_done    = done;

    reg signed [31:0] bias_mem [0:63];

    always @(posedge clk) begin
        if (rst) begin
            r_m <= 0; r_n <= 0; r_k <= 0;
            r_mode <= 0; r_relu <= 1; r_raw <= 0;
            r_mult <= 16'd1; r_shift <= 6'd1;
        end else if (bus_we && is_csr && !busy) begin
            case (bus_addr[7:0])
                8'h08: r_m     <= bus_wdata[15:0];
                8'h0C: r_n     <= bus_wdata[15:0];
                8'h10: r_k     <= bus_wdata[15:0];
                8'h14: {r_raw, r_relu, r_mode} <= bus_wdata[2:0];
                8'h18: r_mult  <= bus_wdata[15:0];
                8'h1C: r_shift <= bus_wdata[5:0];
                default: ;
            endcase
        end
        if (bus_we && is_bias)
            bias_mem[bus_addr[7:2]] <= bus_wdata;
    end

    // ---------------- buffer A, W (mỗi bank 1 cổng ghi host + 1 cổng đọc) ----------------
    reg  [A_AW-1:0] rd_addr_a;
    reg  [W_AW-1:0] rd_addr_w;
    wire [ROWS*8-1:0] a_rd;
    wire [COLS*8-1:0] w_rd;

    genvar gi;
    generate
        for (gi = 0; gi < ROWS; gi = gi + 1) begin : g_abank
            reg [7:0] mem [0:(1<<A_AW)-1];
            reg [7:0] q;
            always @(posedge clk) begin
                if (bus_we && is_a && word[15:A_AW] == gi)
                    mem[word[A_AW-1:0]] <= bus_wdata[7:0];
                q <= mem[rd_addr_a];
            end
            assign a_rd[gi*8 +: 8] = q;
        end
        for (gi = 0; gi < COLS; gi = gi + 1) begin : g_wbank
            reg [7:0] mem [0:(1<<W_AW)-1];
            reg [7:0] q;
            always @(posedge clk) begin
                if (bus_we && is_w && word[15:W_AW] == gi)
                    mem[word[W_AW-1:0]] <= bus_wdata[7:0];
                q <= mem[rd_addr_w];
            end
            assign w_rd[gi*8 +: 8] = q;
        end
    endgenerate

    // ---------------- FSM điều khiển tile ----------------
    localparam S_IDLE = 3'd0, S_CLR = 3'd1, S_FEED = 3'd2, S_DRAIN = 3'd3, S_FLUSH = 3'd4;
    reg [2:0]  state;
    reg [15:0] t;                 // đếm chu kỳ feed
    reg [15:0] row0, col0;        // hàng/cột đầu của tile
    reg [A_AW-1:0] base_a;
    reg [W_AW-1:0] base_w;
    reg [7:0]  di, dj;            // chỉ số drain trong tile
    reg [2:0]  fl;
    reg        rd_valid;

    wire [15:0] feed_len = r_k + ROWS + COLS + 1;   // +2 trễ đọc buffer, dư 1 nhịp an toàn
    wire        last_drain = (di == ROWS - 1) && (dj == COLS - 1);

    always @(posedge clk) begin
        if (rst) begin
            state <= S_IDLE; busy <= 0; done <= 0; r_cycles <= 0;
            t <= 0; row0 <= 0; col0 <= 0; base_a <= 0; base_w <= 0;
            di <= 0; dj <= 0; fl <= 0;
        end else begin
            if (busy) r_cycles <= r_cycles + 1;
            case (state)
                S_IDLE: if (start_pulse) begin
                    busy <= 1; done <= 0; r_cycles <= 0;
                    row0 <= 0; col0 <= 0; base_a <= 0; base_w <= 0;
                    state <= S_CLR;
                end
                S_CLR: begin
                    t <= 0;
                    state <= S_FEED;
                end
                S_FEED: begin
                    t <= t + 1;
                    if (t == feed_len - 1) begin
                        di <= 0; dj <= 0;
                        state <= S_DRAIN;
                    end
                end
                S_DRAIN: begin
                    if (dj == COLS - 1) begin dj <= 0; di <= di + 1; end
                    else dj <= dj + 1;
                    if (last_drain) begin fl <= 0; state <= S_FLUSH; end
                end
                S_FLUSH: begin
                    fl <= fl + 1;
                    if (fl == 3'd3) begin
                        if (col0 + COLS < r_n) begin
                            col0 <= col0 + COLS; base_w <= base_w + r_k[W_AW-1:0];
                            state <= S_CLR;
                        end else if (row0 + ROWS < r_m) begin
                            col0 <= 0; base_w <= 0;
                            row0 <= row0 + ROWS; base_a <= base_a + r_k[A_AW-1:0];
                            state <= S_CLR;
                        end else begin
                            busy <= 0; done <= 1;
                            state <= S_IDLE;
                        end
                    end
                end
                default: state <= S_IDLE;
            endcase
        end
    end

    // địa chỉ đọc buffer trong pha FEED (dữ liệu ra sau 1 chu kỳ)
    always @(posedge clk) begin
        rd_addr_a <= base_a + t[A_AW-1:0];
        rd_addr_w <= base_w + t[W_AW-1:0];
    end
    // rd_addr_* đăng ký ở chu kỳ t, q đọc ở chu kỳ t+1 -> valid trễ 2 nhịp so với t
    reg rv0;
    always @(posedge clk) begin
        rv0      <= (state == S_FEED) && (t < r_k);
        rd_valid <= rv0;
    end

    // ---------------- skew + mask ----------------
    wire [ROWS*8-1:0] a_left;
    wire [COLS*8-1:0] w_top;
    generate
        for (gi = 0; gi < ROWS; gi = gi + 1) begin : g_askew
            wire       ok = rd_valid && ((row0 + gi) < r_m);
            wire [7:0] d  = ok ? a_rd[gi*8 +: 8] : 8'd0;
            if (gi == 0) begin : g0
                assign a_left[7:0] = d;
            end else begin : gd
                npu_delay #(.W(8), .D(gi)) u_d (.clk(clk), .rst(rst), .d(d), .q(a_left[gi*8 +: 8]));
            end
        end
        for (gi = 0; gi < COLS; gi = gi + 1) begin : g_wskew
            wire       ok = rd_valid && ((col0 + gi) < r_n);
            // cột ngoài N: mã 0x07 = PoT zero, 0x00 = INT8 zero -> dùng 0 cho INT8, 7 cho PoT
            wire [7:0] z  = r_mode ? 8'h07 : 8'h00;
            wire [7:0] d  = ok ? w_rd[gi*8 +: 8] : z;
            if (gi == 0) begin : g0
                assign w_top[7:0] = d;
            end else begin : gd
                npu_delay #(.W(8), .D(gi)) u_d (.clk(clk), .rst(rst), .d(d), .q(w_top[gi*8 +: 8]));
            end
        end
    endgenerate

    // ---------------- systolic array ----------------
    wire [ROWS*COLS*32-1:0] acc_flat;
    npu_array #(.ROWS(ROWS), .COLS(COLS), .PE_MODE(PE_MODE)) u_array (
        .clk(clk), .rst(rst),
        .clr(state == S_CLR),
        .en(state == S_FEED),
        .mode(r_mode),
        .a_left(a_left), .w_top(w_top),
        .acc_flat(acc_flat)
    );

    // ---------------- drain -> requant -> OUT ----------------
    wire [15:0] dm = row0 + di;
    wire [15:0] dn = col0 + dj;
    wire        dvalid = (state == S_DRAIN) && (dm < r_m) && (dn < r_n);
    wire signed [31:0] acc_sel = acc_flat[(di*COLS + dj)*32 +: 32];
    wire [31:0] oaddr_full = dm * r_n + dn;

    wire        rq_valid;
    wire [31:0] rq_data;
    wire [12:0] rq_addr;
    npu_requant u_rq (
        .clk(clk), .in_valid(dvalid), .acc(acc_sel), .bias(bias_mem[dn[5:0]]),
        .mult(r_mult), .shift(r_shift), .relu(r_relu), .raw(r_raw),
        .in_addr(oaddr_full[12:0]),
        .out_valid(rq_valid), .out_data(rq_data), .out_addr(rq_addr)
    );

    reg [31:0] out_mem [0:(1<<O_AW)-1];
    always @(posedge clk)
        if (rq_valid) out_mem[rq_addr] <= rq_data;

    // ---------------- đọc từ host ----------------
    always @(posedge clk) begin
        if (bus_re) begin
            if (is_out)
                bus_rdata <= out_mem[word[O_AW-1:0]];
            else if (is_bias)
                bus_rdata <= bias_mem[bus_addr[7:2]];
            else case (bus_addr[7:0])
                8'h04: bus_rdata <= {30'd0, done, busy};
                8'h08: bus_rdata <= {16'd0, r_m};
                8'h0C: bus_rdata <= {16'd0, r_n};
                8'h10: bus_rdata <= {16'd0, r_k};
                8'h14: bus_rdata <= {29'd0, r_raw, r_relu, r_mode};
                8'h18: bus_rdata <= {16'd0, r_mult};
                8'h1C: bus_rdata <= {26'd0, r_shift};
                8'h20: bus_rdata <= r_cycles;
                8'h24: bus_rdata <= {8'hA1, PE_MODE[3:0], 4'h0, ROWS[7:0], COLS[7:0]};
                default: bus_rdata <= 32'd0;
            endcase
        end
    end
endmodule
