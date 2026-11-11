// Testbench hệ thống: nạp dữ liệu qua AXI4-Lite (như PS làm), chạy, đọc kết quả, so với golden.
// Chạy trong thư mục vector (cfg.txt, a.hex, w.hex, bias.hex, exp.hex do export_hw.py sinh).
`timescale 1ns/1ps
module tb_npu_axil;
    localparam ROWS = 8, COLS = 8, DEPTH = 2048;
    reg aclk = 0, aresetn = 0;
    always #5 aclk = ~aclk;            // 100 MHz

    reg  [19:0] awaddr, araddr;
    reg         awvalid = 0, wvalid = 0, bready = 1, arvalid = 0, rready = 1;
    reg  [31:0] wdata;
    wire        awready, wready, bvalid, arready, rvalid, irq;
    wire [1:0]  bresp, rresp;
    wire [31:0] rdata;

    npu_axil #(.ROWS(ROWS), .COLS(COLS)) dut (
        .aclk(aclk), .aresetn(aresetn),
        .s_axi_awaddr(awaddr), .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(4'hF), .s_axi_wvalid(wvalid), .s_axi_wready(wready),
        .s_axi_bresp(bresp), .s_axi_bvalid(bvalid), .s_axi_bready(bready),
        .s_axi_araddr(araddr), .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rdata(rdata), .s_axi_rresp(rresp), .s_axi_rvalid(rvalid), .s_axi_rready(rready),
        .irq(irq));

    task axi_write(input [19:0] addr, input [31:0] data);
        begin
            @(posedge aclk); awaddr <= addr; wdata <= data; awvalid <= 1; wvalid <= 1;
            @(posedge aclk); while (!awready) @(posedge aclk);
            awvalid <= 0; wvalid <= 0;
            while (!bvalid) @(posedge aclk);
        end
    endtask

    task axi_read(input [19:0] addr, output [31:0] data);
        begin
            @(posedge aclk); araddr <= addr; arvalid <= 1;
            @(posedge aclk); while (!arready) @(posedge aclk);
            arvalid <= 0;
            while (!rvalid) @(posedge aclk);
            data = rdata;
        end
    endtask

    reg [7:0]  amem [0:ROWS*DEPTH-1];
    reg [7:0]  wmem [0:COLS*DEPTH-1];
    reg [31:0] bmem [0:63];
    reg [31:0] emem [0:8191];
    integer M, N, K, MODE, RELU, RAW, MULT, SHIFT, fd, r;
    integer i, j, lim, errs, cyc;
    reg [31:0] v;

    initial begin
        fd = $fopen("cfg.txt", "r");
        if (fd == 0) begin $display("khong thay cfg.txt"); $finish; end
        r = $fscanf(fd, "%d %d %d %d %d %d %d %d", M, N, K, MODE, RELU, RAW, MULT, SHIFT);
        $fclose(fd);
        $readmemh("a.hex", amem);
        $readmemh("w.hex", wmem);
        $readmemh("bias.hex", bmem);
        $readmemh("exp.hex", emem, 0, M*N-1);

        repeat (5) @(posedge aclk);
        aresetn = 1;
        axi_read(20'h00024, v);
        $display("HWINFO = %h | M=%0d N=%0d K=%0d mode=%s relu=%0d raw=%0d", v, M, N, K, MODE ? "POT" : "INT8", RELU, RAW);

        lim = ((M + ROWS - 1) / ROWS) * K;
        for (i = 0; i < ROWS; i = i + 1)
            for (j = 0; j < lim; j = j + 1)
                axi_write(20'h40000 + (i*DEPTH + j)*4, amem[i*DEPTH + j]);
        lim = ((N + COLS - 1) / COLS) * K;
        for (i = 0; i < COLS; i = i + 1)
            for (j = 0; j < lim; j = j + 1)
                axi_write(20'h80000 + (i*DEPTH + j)*4, wmem[i*DEPTH + j]);
        for (i = 0; i < N; i = i + 1) axi_write(20'h01000 + i*4, bmem[i]);
        axi_write(20'h00008, M);
        axi_write(20'h0000C, N);
        axi_write(20'h00010, K);
        axi_write(20'h00014, MODE | (RELU << 1) | (RAW << 2));
        axi_write(20'h00018, MULT);
        axi_write(20'h0001C, SHIFT);
        axi_write(20'h00000, 1);                       // start

        v = 0;
        while (!v[1]) axi_read(20'h00004, v);          // polling DONE (driver có thể dùng IRQ)
        axi_read(20'h00020, v); cyc = v;

        errs = 0;
        for (i = 0; i < M*N; i = i + 1) begin
            axi_read(20'hC0000 + i*4, v);
            if (v !== emem[i]) begin
                errs = errs + 1;
                if (errs <= 5) $display("  SAI out[%0d] (m=%0d n=%0d): rtl=%0d golden=%0d", i, i/N, i%N, $signed(v), $signed(emem[i]));
            end
        end
        $display("RESULT %s : %0d/%0d dung, compute = %0d chu ky (%0d MAC, %.1f MAC/chu ky)",
                 errs ? "FAIL" : "PASS", M*N - errs, M*N, cyc, M*N*K, 1.0*M*N*K/cyc);
        $finish;
    end

    initial begin
        #200_000_000;
        $display("TIMEOUT"); $finish;
    end
endmodule
