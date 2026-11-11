# PS + PetaLinux: chạy NPU trên kit SoC

Kit giả định: **Ultra96-V2** (thư mục TRAIN FOR PROJECT có Ultra96-PYNQ). Kit khác chỉ cần đổi `PART`, `BOARD` trong `vivado/create_project.tcl` và địa chỉ base khi chạy app.

## 0. Thử trên PC trước (không cần kit)
```bash
make emu && ./npu_app_emu       # NPU giả lập bằng phần mềm -> kiểm tra driver, layout bank, so khớp golden
```
Kết quả đã chạy: `360 anh | acc NPU = 94.72% | acc CPU = 94.72% | logits lech = 0`.

## 1. Vivado (phần PL)
```bash
cd vivado && vivado -mode batch -source create_project.tcl
```
Sinh `system_wrapper.xsa` (bitstream + mô tả PS). NPU nằm ở `0xA000_0000`, 1 MB, ngắt nối `pl_ps_irq0`.
Script chưa chạy thử được ở môi trường này (không có Vivado) – nếu lỗi ở `apply_bd_automation`, mở GUI và làm tay đúng các bước trong script.

## 2. PetaLinux (Ubuntu 20.04/22.04, PetaLinux cùng version với Vivado)
```bash
petalinux-create -t project --template zynqMP -n npu_os
cd npu_os
petalinux-config --get-hw-description=../vivado/system_wrapper.xsa
#   Image Packaging -> root filesystem type: EXT4 (SD card) cho dễ chép file
cp ../petalinux/system-user.dtsi project-spec/meta-user/recipes-bsp/device-tree/files/
petalinux-config -c kernel     # Device Drivers -> Userspace I/O -> bật UIO_PDRV_GENIRQ
petalinux-create -t apps --template c --name npu-app --enable
cp ../petalinux/npu-app.bb project-spec/meta-user/recipes-apps/npu-app/
cp ../npu_app.c ../npu_drv.[ch] ../../03_golden_c/npu_ref.[ch] \
   ../../03_golden_c/model_params.h ../../03_golden_c/test_vectors.h \
   project-spec/meta-user/recipes-apps/npu-app/files/
petalinux-build
petalinux-package --boot --fsbl images/linux/zynqmp_fsbl.elf --u-boot images/linux/u-boot.elf \
   --pmufw images/linux/pmufw.elf --fpga images/linux/system.bit --force
```
Chép `BOOT.BIN`, `image.ub`, `boot.scr` vào phân vùng FAT của thẻ SD, `rootfs.ext4` vào phân vùng 2.

## 3. Chạy trên kit
```bash
npu_app /dev/uio0            # dùng UIO (khuyên dùng)
npu_app /dev/mem 0xA0000000  # hoặc /dev/mem (cần root)
```
App in: HWINFO, accuracy NPU vs CPU, số ảnh lệch logits (phải = 0), thời gian/ảnh và số chu kỳ NPU.

## 4. Việc còn lại (theo thứ tự)
1. Chạy Vivado, ghi lại utilization/timing/power cho PE_MODE = 0/1/2.
2. Boot PetaLinux, chạy `npu_app` → điền bảng kết quả vào báo cáo.
3. Thay AXI-Lite bằng AXI-DMA khi thời gian nạp dữ liệu chiếm phần lớn (rất có thể).
