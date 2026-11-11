# Chạy phần AI trên Google Colab

Model rất nhỏ nên CPU cũng chạy được, nhưng MNIST + 8 cấu hình QAT trên Colab GPU nhanh hơn nhiều (~3–5 phút).

1. Upload thư mục `02_model_python` lên Google Drive (hoặc kéo thả 3 file `qsim.py`, `model_qat.py`, `export_hw.py` vào Colab).
2. Cell 1:
   ```python
   %cd /content/drive/MyDrive/02_model_python   # hoặc /content nếu upload trực tiếp
   !python model_qat.py --dataset mnist --epochs 4 --qat_epochs 1
   ```
   In bảng accuracy 8 cấu hình, ghi `out/results.json`, `out/chosen.json`, `out/qmodel_*.npz`.
3. Cell 2 – sinh dữ liệu cho C và RTL (thay IPI bằng cấu hình trong chosen.json):
   ```python
   !mkdir -p ../03_golden_c ../05_rtl/sim/vectors
   !python export_hw.py --qmodel out/qmodel_IPI.npz --dataset mnist --ntest 200
   ```
4. Tải `03_golden_c/model_params.h`, `test_vectors.h` và `05_rtl/sim/vectors/` về đè lên bản trong dự án, rồi chạy `make run` (golden C) và `bash 05_rtl/sim/run_all.sh`.

Muốn dán từng cell thay vì chạy script: mỗi khối `# %%` trong `model_qat.py` là một cell (cell đầu đọc tham số bằng `parse_known_args` nên chạy được trong notebook; sửa giá trị mặc định `--dataset` nếu cần).

Bản đang có trong dự án được sinh từ bộ *sklearn digits* 8×8 (máy làm việc không tải được MNIST) – đủ để kiểm chứng cả chuỗi Python → C → RTL, nhưng số accuracy cho báo cáo cần lấy từ lần chạy MNIST trên Colab.
