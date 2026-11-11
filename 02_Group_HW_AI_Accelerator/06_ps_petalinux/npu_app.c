// Ứng dụng PS: chạy Tiny CNN với GEMM trên NPU (PL), im2col + maxpool trên ARM.
// So sánh từng ảnh với golden model chạy thuần CPU, in accuracy và thời gian.
//   ./npu_app [/dev/mem 0xA0000000] | [/dev/uio0]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "npu_drv.h"
#include "../03_golden_c/npu_ref.h"
#include "../03_golden_c/model_params.h"
#include "../03_golden_c/test_vectors.h"

#define H1 IMG
#define H2 (IMG / 2)
static int32_t cols[H1 * H1 * (L1_K > L0_K ? L1_K : L0_K)];
static int32_t ta[L0_N * H1 * H1], tb[L0_N * H1 * H1], to[H1 * H1 * L0_N];

static double now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void hwc_to_chw(const int32_t *o, int HW, int C, int32_t *chw) {
    for (int m = 0; m < HW; m++) for (int c = 0; c < C; c++) chw[c * HW + m] = o[m * C + c];
}

typedef void (*gemm_fn)(const int32_t *, int, int, const uint8_t *, int, int, const int32_t *,
                        int32_t, int, int, int, int32_t *);
static uint32_t hw_cycles;

static void gemm_hw(const int32_t *A, int M, int K, const uint8_t *W, int N, int mode, const int32_t *b,
                    int32_t mult, int shift, int relu, int raw, int32_t *out) {
    npu_layer_cfg c = {M, N, K, mode, relu, raw, mult, shift};
    uint32_t cyc = 0;
    if (npu_gemm(A, W, b, &c, out, &cyc) != 0) exit(2);
    hw_cycles += cyc;
}

static int infer(const uint8_t *img, int32_t *lg, gemm_fn g) {
    for (int i = 0; i < IMG * IMG; i++) ta[i] = img[i];
    npu_im2col3x3(ta, 1, H1, H1, cols);
    g(cols, H1 * H1, L0_K, L0_W, L0_N, L0_MODE, L0_B, L0_MULT, L0_SHIFT, L0_RELU, L0_RAW, to);
    hwc_to_chw(to, H1 * H1, L0_N, tb);
    npu_maxpool2(tb, L0_N, H1, H1, ta);
    npu_im2col3x3(ta, L0_N, H2, H2, cols);
    g(cols, H2 * H2, L1_K, L1_W, L1_N, L1_MODE, L1_B, L1_MULT, L1_SHIFT, L1_RELU, L1_RAW, to);
    hwc_to_chw(to, H2 * H2, L1_N, tb);
    npu_maxpool2(tb, L1_N, H2, H2, ta);
    g(ta, 1, L2_K, L2_W, L2_N, L2_MODE, L2_B, L2_MULT, L2_SHIFT, L2_RELU, L2_RAW, lg);
    int best = 0;
    for (int i = 1; i < L2_N; i++) if (lg[i] > lg[best]) best = i;
    return best;
}

int main(int argc, char **argv) {
    const char *dev = argc > 1 ? argv[1] : "/dev/mem";
    uint64_t base = argc > 2 ? strtoull(argv[2], NULL, 0) : 0xA0000000ULL;   // HPM0_FPD trên ZynqMP
    if (npu_open(dev, base) != 0) return 1;
    uint32_t info = npu_rd(NPU_HWINFO);
    printf("NPU HWINFO=0x%08x (ROWS=%u COLS=%u PE_MODE=%u)\n", info, (info >> 8) & 0xFF, info & 0xFF, (info >> 20) & 0xF);

    int ok_hw = 0, ok_cpu = 0, mism = 0;
    int32_t lh[L2_N], lc[L2_N];
    double t_hw = 0, t_cpu = 0, t0;
    for (int t = 0; t < N_TEST; t++) {
        const uint8_t *img = &TEST_IMG[t * IMG * IMG];
        t0 = now_ms(); int ph = infer(img, lh, gemm_hw);      t_hw += now_ms() - t0;
        t0 = now_ms(); int pc = infer(img, lc, npu_gemm_ref); t_cpu += now_ms() - t0;
        ok_hw += ph == TEST_LABEL[t];
        ok_cpu += pc == TEST_LABEL[t];
        if (memcmp(lh, lc, sizeof(lh)) || memcmp(lh, &TEST_LOGIT[t * L2_N], sizeof(lh))) mism++;
    }
    printf("%d anh | acc NPU = %.2f%% | acc CPU = %.2f%% | logits lech = %d\n",
           N_TEST, 100.0 * ok_hw / N_TEST, 100.0 * ok_cpu / N_TEST, mism);
    printf("thoi gian/anh: NPU path %.3f ms (gom nap AXI-Lite), CPU %.3f ms | NPU compute %.1f chu ky/anh\n",
           t_hw / N_TEST, t_cpu / N_TEST, (double)hw_cycles / N_TEST);
    npu_close();
    return mism ? 1 : 0;
}
