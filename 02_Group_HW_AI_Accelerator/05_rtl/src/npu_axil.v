// Top IP: AXI4-Lite slave (32 bit, 1 MB) bọc npu_core. Gắn vào M_AXI_HPM0 của Zynq PS qua SmartConnect.
module npu_axil #(
    parameter ROWS    = 8,
    parameter COLS    = 8,
    parameter PE_MODE = 0
) (
    input  wire        aclk,
    input  wire        aresetn,
    input  wire [19:0] s_axi_awaddr,
    input  wire        s_axi_awvalid,
    output reg         s_axi_awready,
    input  wire [31:0] s_axi_wdata,
    input  wire [3:0]  s_axi_wstrb,
    input  wire        s_axi_wvalid,
    output reg         s_axi_wready,
    output wire [1:0]  s_axi_bresp,
    output reg         s_axi_bvalid,
    input  wire        s_axi_bready,
    input  wire [19:0] s_axi_araddr,
    input  wire        s_axi_arvalid,
    output reg         s_axi_arready,
    output wire [31:0] s_axi_rdata,
    output wire [1:0]  s_axi_rresp,
    output reg         s_axi_rvalid,
    input  wire        s_axi_rready,
    output wire        irq
);
    wire rst = ~aresetn;
    assign s_axi_bresp = 2'b00;
    assign s_axi_rresp = 2'b00;

    reg        we_q, re_q, re_d, rd_busy;
    reg [19:0] waddr_q, raddr_q;
    reg [31:0] wdata_q;

    // ghi: nhận AW và W cùng lúc, phát 1 xung bus_we, rồi trả B
    always @(posedge aclk) begin
        if (rst) begin
            s_axi_awready <= 0; s_axi_wready <= 0; s_axi_bvalid <= 0; we_q <= 0;
        end else begin
            we_q <= 0;
            s_axi_awready <= 0; s_axi_wready <= 0;
            if (s_axi_awvalid && s_axi_wvalid && !s_axi_awready && !s_axi_bvalid && !rd_busy) begin
                s_axi_awready <= 1; s_axi_wready <= 1;
                waddr_q <= s_axi_awaddr; wdata_q <= s_axi_wdata; we_q <= 1;
            end
            if (s_axi_awready) s_axi_bvalid <= 1;
            else if (s_axi_bvalid && s_axi_bready) s_axi_bvalid <= 0;
        end
    end

    // đọc: core trả dữ liệu sau 1 nhịp
    always @(posedge aclk) begin
        if (rst) begin
            s_axi_arready <= 0; s_axi_rvalid <= 0; re_q <= 0; re_d <= 0; rd_busy <= 0;
        end else begin
            re_q <= 0; s_axi_arready <= 0;
            re_d <= re_q;
            if (s_axi_arvalid && !rd_busy && !we_q && !(s_axi_awvalid && s_axi_wvalid)) begin
                s_axi_arready <= 1; raddr_q <= s_axi_araddr; re_q <= 1; rd_busy <= 1;
            end
            if (re_d) s_axi_rvalid <= 1;
            else if (s_axi_rvalid && s_axi_rready) begin s_axi_rvalid <= 0; rd_busy <= 0; end
        end
    end

    npu_core #(.ROWS(ROWS), .COLS(COLS), .PE_MODE(PE_MODE)) u_core (
        .clk(aclk), .rst(rst),
        .bus_we(we_q), .bus_re(re_q),
        .bus_addr(we_q ? waddr_q : raddr_q),
        .bus_wdata(wdata_q), .bus_rdata(s_axi_rdata),
        .irq_done(irq)
    );
endmodule
