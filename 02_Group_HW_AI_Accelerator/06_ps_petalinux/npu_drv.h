// Driver userspace cho NPU (AXI4-Lite). Trên kit: mmap /dev/mem hoặc /dev/uioX.
// Build với -DNPU_EMU để chạy trên PC: thanh ghi được giả lập bằng phần mềm (kiểm tra layout/banking).
#pragma once
#include <stdint.h>

#define NPU_ROWS     8
#define NPU_COLS     8
#define NPU_BANK_D   2048
#define NPU_OUT_D    8192

#define NPU_CTRL     0x00
#define NPU_STATUS   0x04
#define NPU_M        0x08
#define NPU_N        0x0C
#define NPU_K        0x10
#define NPU_CFG      0x14
#define NPU_MULT     0x18
#define NPU_SHIFT    0x1C
#define NPU_CYCLES   0x20
#define NPU_HWINFO   0x24
#define NPU_BIAS     0x01000
#define NPU_ABUF     0x40000
#define NPU_WBUF     0x80000
#define NPU_OBUF     0xC0000
#define NPU_SPAN     0x100000

typedef struct {
    int M, N, K, mode, relu, raw;
    int32_t mult;
    int shift;
} npu_layer_cfg;

int      npu_open(const char *dev, uint64_t phys_base);   // dev = "/dev/mem" hoặc "/dev/uio0"
void     npu_close(void);
uint32_t npu_rd(uint32_t off);
void     npu_wr(uint32_t off, uint32_t v);

// A[m*K+k] (int8 dạng int32), W[k*N+n] (byte: int8 hoặc mã PoT)
int      npu_gemm(const int32_t *A, const uint8_t *W, const int32_t *bias,
                  const npu_layer_cfg *c, int32_t *out, uint32_t *cycles);
