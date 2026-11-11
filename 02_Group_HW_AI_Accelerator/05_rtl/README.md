# RTL NPU DM-SA

| File | Nội dung |
|---|---|
| src/npu_pe.v | PE 2 chế độ INT8 / PoT, tham số PE_MODE |
| src/npu_array.v | mảng ROWS×COLS |
| src/npu_delay.v | đường trễ cho skew |
| src/npu_requant.v | bias + requant + ReLU, 2 tầng pipeline |
| src/npu_core.v | buffer, CSR, FSM tile, drain |
| src/npu_axil.v | top AXI4-Lite |
| tb/tb_pe.v | kiểm tra vét cạn 1 PE |
| tb/tb_npu_axil.v | nạp qua AXI-Lite, chạy, so với golden |
| sim/vectors/* | vector do `02_model_python/export_hw.py` sinh |

```bash
bash sim/run_all.sh      # cần iverilog >= 11
```
Kết quả hiện tại (`sim/sim_result.txt`): tất cả PASS. Ước lượng tài nguyên PE: `sim/yosys_pe_report.txt`.
