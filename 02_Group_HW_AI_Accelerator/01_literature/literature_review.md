# Khảo sát tài liệu – Kiến trúc phần cứng cho model AI nhẹ

**Hướng nghiên cứu chọn:** *Tối ưu datapath bằng phép toán xấp xỉ – lượng tử Power-of-Two (PoT) thay phép nhân bằng phép dịch, kết hợp INT8 theo từng layer, trên một systolic array có tham số tĩnh/động.*
(Thuộc nhóm "phép toán xấp xỉ" + "parameterized tĩnh/động" trong lộ trình thầy giao.)

> Cách tra cứu: dùng OpenAlex (script `openalex_search.py` cùng thư mục, chạy trên Colab hoặc máy có mạng) với các từ khoá trong bảng cuối file. Tải full-text: ưu tiên bản arXiv / open-access (trường `open_access.oa_url` của OpenAlex), sau đó tới IEEE Xplore qua tài khoản thư viện trường.
> Số liệu dưới đây lấy từ abstract/trang chính thức của từng bài. Trước khi đưa vào báo cáo cuối, nhóm cần đọc full-text để kiểm tra lại các con số.

## 1. Bảng tổng hợp

| # | Bài báo | Nơi công bố | Đã làm được | Chưa làm / giới hạn (liên quan đề tài) |
|---|---------|-------------|-------------|-----------------------------------------|
| 1 | Miyashita, Lee, Murmann – *CNNs using Logarithmic Data Representation* | arXiv:1603.01025 (2016) | Biểu diễn log2 cho weight + activation, mã hoá tới 3 bit gần như không mất accuracy, bỏ được bộ nhân | Chưa có kiến trúc FPGA hoàn chỉnh, chưa xét kết hợp với INT8 |
| 2 | Gudovskiy, Rigazio – *ShiftCNN* | arXiv:1706.02393 (2017) | Weight PoT, chỉ dùng shift + add; chuyển đổi không cần train lại, giảm <1% accuracy ImageNet; mô phỏng RTL cho thấy công suất lớp conv giảm ~4× so với 8-bit fixed-point | Kết quả công suất từ mô phỏng, không có hệ thống SoC end-to-end |
| 3 | Jacob et al. – *Quantization and Training for Integer-Arithmetic-Only Inference* | CVPR 2018, doi:10.1109/CVPR.2018.00286 | Chuẩn lượng tử INT8 (scale + zero-point, requant bằng multiplier + shift) + QAT; nền của TFLite | Đồng nhất 8 bit cho mọi layer, không khai thác non-uniform |
| 4 | Li, Dong, Wang – *Additive Powers-of-Two (APoT)* | ICLR 2020, arXiv:1909.13144 | Mức lượng tử là tổng các luỹ thừa 2, khớp phân bố chuông; ResNet-50 4 bit đạt 76.6% top-1, giảm 22% chi phí tính toán so với uniform | Đề xuất ở mức thuật toán, chưa có phần cứng |
| 5 | Chang et al. – *Mix and Match (MSQ)* | HPCA 2021, doi:10.1109/HPCA51647.2021.00027 | Trộn SP2 (sum-of-power-of-2, chạy trên LUT) và fixed-point (chạy trên DSP) **theo hàng** của ma trận weight; 2 lõi GEMM dị thể; nhanh hơn 2.1–4.1× so với chỉ dùng DSP trên XC7Z020/XC7Z045 | Tỷ lệ chia 2 lõi cố định lúc thiết kế → layer có tỷ lệ khác sẽ để một lõi rảnh; dùng HLS |
| 6 | Saha et al. – *PoTAcc* | IEEE TCASAI 2026, doi:10.1109/TCASAI.2026.3690415 (arXiv:2605.06082) | Pipeline mã nguồn mở end-to-end cho DNN lượng tử PoT qua TFLite; shift-PE cho 3 kiểu PoT (QKeras, MSQ, APoT); PYNQ-Z2 & Kria; tới 3.6× nhanh hơn và giảm 78% năng lượng so với chỉ CPU | Một kiểu PoT cho cả model; chính tác giả nêu hướng mở: tối ưu dataflow/buffer, thêm loại layer |
| 7 | Liu, Ullah, Kumar – *Bitwise Systolic Array (BitSys)* | ISQED 2025, doi:10.1109/ISQED65160.2025.11014376 | Systolic array bit-wise, đổi độ chính xác 8/4/2/1 bit **lúc chạy**, Ultra96; nhanh hơn 1.32–3.57× so với các công trình trước khi chạy model mixed-precision | Chỉ lượng tử uniform; không có PoT |
| 8 | *DyBit* | arXiv:2302.12510 (2023) | Định dạng bit động + accelerator systolic mixed-precision cấu hình lúc chạy | Cần bộ giải mã/encoder riêng, phức tạp hơn shift-PE |
| 9 | Chen, Krishna, Emer, Sze – *Eyeriss* | IEEE JSSC 2017, doi:10.1109/JSSC.2016.2616357 | Dataflow row-stationary, 168 PE, tối ưu di chuyển dữ liệu | ASIC, không bàn lượng tử PoT |
| 10 | Umuroglu et al. – *FINN* | FPGA 2017, arXiv:1612.07119 | Framework streaming cho BNN; 12.3 triệu ảnh/s MNIST, 95.8% trên ZC706 | Nhị phân → mất accuracy với bài toán khó; mỗi model một bitstream |
| 11 | Jouppi et al. – *TPU* | ISCA 2017, doi:10.1145/3079856.3080246 | Systolic array 256×256 INT8 weight-stationary trong datacenter | Quy mô lớn, không hướng edge FPGA |
| 12 | Sze et al. – *Efficient Processing of DNNs: A Tutorial and Survey* | Proc. IEEE 2017, doi:10.1109/JPROC.2017.2761740 | Khung phân loại dataflow (WS/OS/RS), chi phí năng lượng truy cập bộ nhớ | Survey |
| 13 | Duarte et al. – *hls4ml* | JINST 2018, doi:10.1088/1748-0221/13/07/P07027 | Sinh firmware FPGA từ model Keras bằng HLS, độ trễ cực thấp | HLS, ít quyền kiểm soát datapath |
| 14 | sun-jay – *FPGA-Hardware-NN-Accelerator* (repo tham khảo trong thư mục TRAIN FOR PROJECT) | GitHub | Systolic array MAC viết Verilog tay, testbench, so sánh thời gian với CPU | Chỉ INT, không có SoC/driver, không lượng tử xấp xỉ |

## 2. Những gì cộng đồng đã làm được

- Lượng tử INT8 gần như không mất accuracy và đã thành chuẩn công nghiệp [3].
- Lượng tử PoT/APoT 3–4 bit giữ accuracy tốt nhờ khớp phân bố chuông của weight, và cho phép thay bộ nhân bằng bộ dịch [1][2][4].
- Trên FPGA đã có: lõi dị thể LUT/DSP cho mixed-scheme [5], pipeline PoT end-to-end trên PYNQ/Kria [6], systolic array đổi bit-width lúc chạy trên Ultra96 [7].

## 3. Khoảng trống (GAP)

1. **Chọn scheme theo layer nhưng tài nguyên cố định:** MSQ [5] trộn PoT/fixed trong layer nhưng tỷ lệ 2 lõi GEMM cố định lúc tổng hợp; với model nhỏ, layer đầu (K nhỏ, nhạy) và layer giữa (K lớn, chịu được PoT) có nhu cầu rất khác nhau.
2. **PoT đồng nhất cho cả model:** PoTAcc [6] dùng một kiểu PoT cho toàn model; chưa đánh giá chọn INT8/PoT **theo từng layer bằng một thanh ghi cấu hình** trên cùng một mảng PE.
3. **Reconfigurable chỉ theo bit-width:** BitSys [7] và DyBit [8] đổi bit-width uniform lúc chạy, không có chế độ phép toán xấp xỉ (shift).
4. **Thiếu thiết kế mở, viết RTL tay, kiểm chứng bit-exact 3 tầng** (Python → C golden → RTL) và driver PetaLinux đủ nhỏ cho đồ án sinh viên trên kit SoC giá thấp.

## 4. Từ khoá tra cứu OpenAlex đề nghị

`power-of-two quantization FPGA`, `shift-add neural network accelerator`, `logarithmic quantization hardware`, `mixed-precision systolic array FPGA`, `runtime reconfigurable precision accelerator`, `hardware-aware mixed precision quantization`, `multiplier-less CNN accelerator`.

## Nguồn

- [1] https://arxiv.org/abs/1603.01025
- [2] https://arxiv.org/abs/1706.02393
- [3] https://openaccess.thecvf.com/content_cvpr_2018/html/Jacob_Quantization_and_Training_CVPR_2018_paper.html
- [4] https://arxiv.org/abs/1909.13144
- [5] https://arxiv.org/abs/2012.04240
- [6] https://arxiv.org/abs/2605.06082 – https://eprints.gla.ac.uk/385896/
- [7] https://arxiv.org/abs/2602.23334
- [8] https://arxiv.org/abs/2302.12510
- [9] https://people.csail.mit.edu/emer/media/papers/2017.01.jssc.eyeriss_design.pdf
- [10] https://arxiv.org/abs/1612.07119
- [11] https://doi.org/10.1145/3079856.3080246
- [12] https://doi.org/10.1109/JPROC.2017.2761740
- [13] https://doi.org/10.1088/1748-0221/13/07/P07027
- [14] https://github.com/sun-jay/FPGA-Hardware-NN-Accelerator
