# Đồ án RTOS – HK2 2025-2026 (GVHD: Huỳnh Hoàng Hà)

Thư mục gồm đủ 2 project theo đề "Task GK + CK". Video chạy thử project cá nhân: [`media/demo_esp32s3.mp4`](media/demo_esp32s3.mp4).

| Project | Tỷ trọng | Thư mục |
|---|---|---|
| Cá nhân: FreeRTOS trên ESP32 | 70% | `01_Personal_FreeRTOS_ESP32/` |
| Nhóm: Kiến trúc HW cho model AI nhẹ trên kit SoC | 30% | `02_Group_HW_AI_Accelerator/` |
| Báo cáo + slide tiến độ | – | `03_Reports/` |

```
RTOS_Project/
├── 01_Personal_FreeRTOS_ESP32/      PlatformIO + Wokwi, 8 task, 12 kỹ thuật FreeRTOS
├── 02_Group_HW_AI_Accelerator/
│   ├── 01_literature/               khảo sát paper, GAP, đề xuất, script OpenAlex
│   ├── 02_model_python/             Tiny CNN + QAT INT8/PoT (chạy Colab), qsim số nguyên, export
│   ├── 03_golden_c/                 golden model C khớp bit
│   ├── 04_hw_spec/                  đặc tả kiến trúc NPU (dataflow, FSM, CSR, hiệu năng)
│   ├── 05_rtl/                      Verilog NPU + testbench + vector + kết quả mô phỏng
│   └── 06_ps_petalinux/             driver/app PS, giả lập trên PC, Vivado TCL, PetaLinux
├── 03_Reports/
    ├── Report_Project_CaNhan_FreeRTOS_v2.docx   báo cáo cá nhân (bản mới, ít code, nhiều hình)
    ├── Report_Project_CaNhan_FreeRTOS.docx      bản cũ
    ├── Slide_Tuan2_ProjectCaNhan.pptx
    ├── Slide_Tuan3_ProjectCaNhan.pptx
    ├── Slide_Tuan4_ProjectCaNhan.pptx
    └── Slide_ProjectNhom_NPU_DMSA.pptx
└── media/demo_esp32s3.mp4          video chạy trên kit
```

## Trạng thái kiểm chứng

| Hạng mục | Đã kiểm tra | Cách kiểm tra |
|---|---|---|
| Firmware ESP32-S3 | Build + nạp + chạy thật OK (RAM 7.1%, Flash 11.5%) | VS Code + PlatformIO, ảnh trong `01_.../docs/screenshots` |
| Nút nhấn, LED3 3 s, OLED, monitor | Đạt (LED3 3 000 ms 5/5, drop=0 sau 10 phút) | log Serial + video; còn thiếu thử nhấn BTN2 liên tiếp, BTN1 khi LED3 sáng |
| Cảm biến ánh sáng | Một phần: đang dùng module hồng ngoại | thay module quang trở cùng chân GPIO1 |
| Model + lượng tử | Chạy OK trên sklearn digits | `python model_qat.py --dataset digits` |
| Model trên MNIST | **Chưa** (máy làm việc không tải được MNIST) | chạy trên Colab, xem `02_model_python/COLAB.md` |
| Golden C ↔ Python | 360/360 ảnh khớp bit | `03_golden_c/golden_result.txt` |
| RTL ↔ golden | 10/10 bộ test PASS, PE 34 816 tổ hợp | `05_rtl/sim/sim_result.txt` |
| Driver PS | Khớp golden trên NPU giả lập | `06_ps_petalinux: make emu` |
| Vivado / PetaLinux / kit | **Chưa** (cần Vivado + kit) | `06_ps_petalinux/README.md` |

## Việc cần làm tiếp
1. Điền họ tên, MSSV trong report; thử nốt kiểm thử 5, 6; tìm lỗi bộ đếm bị đặt lại (mục 6.3 report).
2. Chạy MNIST trên Colab → chạy lại `export_hw.py` → `make run` + `bash 05_rtl/sim/run_all.sh` → cập nhật số liệu slide nhóm.
3. Vivado + PetaLinux trên kit của lớp (mặc định script là Ultra96-V2, đổi part nếu khác).
