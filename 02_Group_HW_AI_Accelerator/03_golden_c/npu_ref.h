// Golden model của NPU: các hàm số nguyên khớp bit với qsim.py và RTL.
// Dùng chung cho chương trình kiểm tra trên PC và ứng dụng PS trên kit (so sánh kết quả HW).
#pragma once
#include <stdint.h>

#define NPU_MODE_INT8 0
#define NPU_MODE_POT  1

static inline int32_t npu_decode_w(uint8_t code, int mode) {
    if (mode == NPU_MODE_INT8) return (int8_t)code;
    int e = code & 7;
    if (e == 7) return 0;
    int32_t mag = 1 << e;
    return (code & 8) ? -mag : mag;
}

// Một PE: INT8 dùng nhân, PoT dùng dịch trái (đúng như phần cứng)
static inline int32_t npu_pe_mul(int32_t a, uint8_t code, int mode) {
    if (mode == NPU_MODE_INT8) return a * (int8_t)code;
    int e = code & 7;
    if (e == 7) return 0;
    int32_t p = a << e;
    return (code & 8) ? -p : p;
}

static inline int32_t npu_requant(int64_t acc, int32_t mult, int shift, int relu) {
    int64_t y = (acc * mult + ((int64_t)1 << (shift - 1))) >> shift;   // dịch số học
    int64_t lo = relu ? 0 : -128;
    if (y < lo) y = lo;
    if (y > 127) y = 127;
    return (int32_t)y;
}

// out[m*N+n] = requant(sum_k A[m*K+k] * W[k*N+n] + bias[n])
void npu_gemm_ref(const int32_t *A, int M, int K, const uint8_t *W, int N, int mode,
                  const int32_t *bias, int32_t mult, int shift, int relu, int raw, int32_t *out);

void npu_im2col3x3(const int32_t *in_chw, int C, int H, int Wd, int32_t *cols);
void npu_maxpool2(const int32_t *in_chw, int C, int H, int Wd, int32_t *out);
