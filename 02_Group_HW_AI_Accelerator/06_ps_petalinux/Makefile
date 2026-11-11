# Trên PC (giả lập NPU):  make emu && ./npu_app_emu
# Cross-compile cho kit:   make CC=aarch64-linux-gnu-gcc   (hoặc dùng recipe PetaLinux, xem README)
CFLAGS ?= -O2 -Wall -Wextra -std=gnu11
SRC = npu_app.c npu_drv.c ../03_golden_c/npu_ref.c
npu_app: $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)
emu: $(SRC)
	$(CC) $(CFLAGS) -DNPU_EMU -o npu_app_emu $(SRC)
clean:
	rm -f npu_app npu_app_emu
