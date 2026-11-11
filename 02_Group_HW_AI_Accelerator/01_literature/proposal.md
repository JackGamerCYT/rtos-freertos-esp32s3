# Đề xuất: DM-SA – Systolic array 2 chế độ INT8 / Power-of-Two chọn theo từng layer lúc chạy

## 1. Ý tưởng

Một mảng PE duy nhất (8×8, output-stationary). Mỗi PE có 2 đường tính:

- **INT8:** `a × w` (bộ nhân, map vào DSP48).
- **PoT:** weight 4 bit `{sign, e}` → `±(a <<< e)` (chỉ cần bộ dịch + bộ đảo dấu, chạy trên LUT).

Thanh ghi `CFG.mode` chọn đường tính **cho từng lần gọi GEMM**, tức là cho từng layer. Phần mềm (Python) tìm cấu hình INT8/PoT cho từng layer theo ràng buộc accuracy + chi phí phần cứng (hardware-aware search). Tham số Verilog `PE_MODE` cho phép tổng hợp 3 biến thể để so sánh (tham số tĩnh vs động):

| PE_MODE | Ý nghĩa | Dùng để |
|---|---|---|
| 0 | dual, chọn lúc chạy | thiết kế đề xuất |
| 1 | chỉ INT8 | baseline |
| 2 | chỉ PoT | baseline không DSP |

## 2. Đóng góp so với GAP

| GAP (xem literature_review.md) | Đề xuất trả lời thế nào |
|---|---|
| MSQ: tỷ lệ lõi LUT/DSP cố định lúc tổng hợp | Một mảng, đổi chế độ theo layer bằng 1 bit CSR, không có lõi rảnh |
| PoTAcc: một kiểu PoT cho cả model | Chọn INT8/PoT riêng từng layer, có bước tìm kiếm theo chi phí |
| BitSys/DyBit: chỉ đổi bit-width uniform | Chế độ thứ 2 là phép toán xấp xỉ (shift), weight còn 4 bit |
| Thiếu thiết kế mở, kiểm chứng bit-exact | Python ↔ golden C ↔ RTL khớp từng bit, có driver PS |

## 3. Bằng chứng đã có (giai đoạn 1, chạy thật)

**Model** (`02_model_python`): Tiny CNN conv3×3(8) – pool – conv3×3(16) – pool – FC(10). Thử nhanh trên bộ *sklearn digits* 8×8 (máy không tải được MNIST; chạy MNIST trên Colab bằng `--dataset mnist`):

| Cấu hình (L0 L1 L2) | Acc số nguyên | Bit weight | Năng lượng MAC tương đối* |
|---|---|---|---|
| FP32 | 94.44% | – | – |
| I I I (all INT8) | 95.00% | 14 912 | 23 680 |
| **I P I (được chọn)** | **94.72%** | **10 304 (−31%)** | **10 778 (−54%)** |
| P P P (all PoT) | 93.89% | 7 456 | 7 104 |

\* giả định 1 MAC PoT tốn 0.3 lần MAC INT8 – **cần thay bằng số đo power của Vivado**.

**Phần cứng 1 PE** (Yosys `synth_xilinx`, họ xc7, chỉ là ước lượng – `05_rtl/sim/yosys_pe_report.txt`):

| PE_MODE | LUT | FF | DSP |
|---|---|---|---|
| 1 – INT8 (có DSP) | 1 | 16 | 1 |
| 1 – INT8 (cấm DSP) | 190 | 48 | 0 |
| 2 – PoT | 69 | 48 | 0 |
| 0 – Dual (có DSP) | 95 | 48 | 1 |

Nhận xét thẳng: PE dual tốn thêm ~95 LUT/PE so với PE INT8 thuần (DSP hấp thụ cả bộ cộng tích luỹ). Lợi ích của dual nằm ở **bộ nhớ weight (4 bit)**, **năng lượng** (tắt DSP trong layer PoT) và **khả năng chạy khi thiếu DSP**, không nằm ở diện tích. Hướng cải tiến giai đoạn 2 để bù diện tích: ở chế độ PoT mỗi PE xử lý **2 weight 4 bit/chu kỳ** (2 bộ dịch, ~+69 LUT) → thông lượng ×2 cho layer PoT.

**Kiểm chứng:** golden C khớp bit 360/360 ảnh với Python; RTL qua AXI4-Lite khớp 100% trên 10 bộ test (3 layer thật + 4 ngẫu nhiên + 3 đúng kích thước MNIST); PE kiểm tra vét cạn 34 816 tổ hợp.

## 4. Kế hoạch đánh giá (cho báo cáo/LVTN)

1. Chạy MNIST (và CIFAR-10 với model lớn hơn) trên Colab → bảng accuracy 8 cấu hình.
2. Tổng hợp Vivado cho Ultra96-V2 (hoặc kit lớp dùng) 3 biến thể PE_MODE, mảng 8×8: LUT/FF/DSP/BRAM, Fmax, power.
3. Đo trên kit: thời gian/ảnh NPU vs ARM Cortex-A53, năng lượng/ảnh.
4. Ablation: chọn theo layer vs all-INT8 vs all-PoT; drain thường vs drain chồng lấp; AXI-Lite vs AXI-DMA.

## 5. Rủi ro và cách xử lý

| Rủi ro | Xử lý |
|---|---|
| Thời gian nạp qua AXI-Lite lớn hơn thời gian tính | Giai đoạn 2: AXI-DMA + AXI-Stream, đóng gói 4 byte/word |
| Drain 64 chu kỳ/tile làm hiệu suất thấp (6–29 MAC/chu kỳ trên 64) | Double-buffer accumulator, drain song song với tile kế tiếp |
| Kit thật khác Ultra96 | RTL độc lập board; chỉ đổi part + địa chỉ base trong script Vivado và driver |
