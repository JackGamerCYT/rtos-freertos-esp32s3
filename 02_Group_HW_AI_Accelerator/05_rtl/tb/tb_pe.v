// Kiểm tra vét cạn 1 PE: mọi a (0..127) x mọi mã trọng số (256 INT8, 16 PoT)
`timescale 1ns/1ps
module tb_pe;
    reg clk = 0, rst = 1, clr = 0, en = 0, mode = 0;
    reg  [7:0] a, w;
    wire [7:0] ao, wo;
    wire signed [31:0] acc;
    npu_pe dut (.clk(clk), .rst(rst), .clr(clr), .en(en), .mode(mode),
                .a_in(a), .w_in(w), .a_out(ao), .w_out(wo), .acc(acc));
    always #5 clk = ~clk;

    function integer ref_mul(input integer av, input integer wc, input integer md);
        integer e, mag;
        begin
            if (md == 0) ref_mul = av * ((wc > 127) ? wc - 256 : wc);
            else begin
                e = wc & 7;
                mag = (e == 7) ? 0 : (av << e);
                ref_mul = (wc & 8) ? -mag : mag;
            end
        end
    endfunction

    integer ai, wi, md, errs = 0, n = 0;
    initial begin
        a = 0; w = 0;
        repeat (2) @(posedge clk);
        rst = 0;
        for (md = 0; md < 2; md = md + 1)
            for (ai = 0; ai < 128; ai = ai + 1)
                for (wi = 0; wi < (md ? 16 : 256); wi = wi + 1) begin
                    @(negedge clk); mode = md; a = ai; w = wi; clr = 1; en = 0;
                    @(negedge clk); clr = 0; en = 1;
                    @(negedge clk); en = 0;
                    if (acc !== ref_mul(ai, wi, md)) begin
                        errs = errs + 1;
                        if (errs < 5) $display("SAI mode=%0d a=%0d w=%0d acc=%0d ref=%0d", md, ai, wi, acc, ref_mul(ai, wi, md));
                    end
                    n = n + 1;
                end
        $display("tb_pe: %0d truong hop, %0d loi -> %s", n, errs, errs ? "FAIL" : "PASS");
        $finish;
    end
endmodule
