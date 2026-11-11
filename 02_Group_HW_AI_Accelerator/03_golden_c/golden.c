// Golden model C cho Tiny CNN (conv-pool-conv-pool-fc), chỉ dùng số nguyên.
// Build: make   |  Chạy: ./golden   (so khớp bit với logits do Python tính)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "npu_ref.h"
#include "model_params.h"
#include "test_vectors.h"

#define C1 L0_N
#define C2 L1_N
#define H1 IMG
#define H2 (IMG / 2)
#define H3 (IMG / 4)

static int32_t buf_cols[H1 * H1 * L1_K > H1 * H1 * L0_K ? H1 * H1 * L1_K : H1 * H1 * L0_K];
static int32_t buf_a[C1 * H1 * H1], buf_b[C1 * H1 * H1], buf_o[H1 * H1 * C1];

// out[m][n] (HWC) -> CHW
static void hwc_to_chw(const int32_t *o, int H, int Wd, int C, int32_t *chw) {
    for (int m = 0; m < H * Wd; m++)
        for (int c = 0; c < C; c++) chw[c * H * Wd + m] = o[m * C + c];
}

int infer(const uint8_t *img, int32_t logits[L2_N]) {
    for (int i = 0; i < IMG * IMG; i++) buf_a[i] = img[i];

    npu_im2col3x3(buf_a, 1, H1, H1, buf_cols);
    npu_gemm_ref(buf_cols, H1 * H1, L0_K, L0_W, C1, L0_MODE, L0_B, L0_MULT, L0_SHIFT, L0_RELU, L0_RAW, buf_o);
    hwc_to_chw(buf_o, H1, H1, C1, buf_b);
    npu_maxpool2(buf_b, C1, H1, H1, buf_a);

    npu_im2col3x3(buf_a, C1, H2, H2, buf_cols);
    npu_gemm_ref(buf_cols, H2 * H2, L1_K, L1_W, C2, L1_MODE, L1_B, L1_MULT, L1_SHIFT, L1_RELU, L1_RAW, buf_o);
    hwc_to_chw(buf_o, H2, H2, C2, buf_b);
    npu_maxpool2(buf_b, C2, H2, H2, buf_a);          // flatten CHW = thứ tự của PyTorch

    npu_gemm_ref(buf_a, 1, L2_K, L2_W, L2_N, L2_MODE, L2_B, L2_MULT, L2_SHIFT, L2_RELU, L2_RAW, logits);
    int best = 0;
    for (int i = 1; i < L2_N; i++) if (logits[i] > logits[best]) best = i;
    return best;
}

int main(void) {
    int correct = 0, mismatch = 0;
    int32_t lg[L2_N];
    for (int t = 0; t < N_TEST; t++) {
        int pred = infer(&TEST_IMG[t * IMG * IMG], lg);
        correct += (pred == TEST_LABEL[t]);
        if (memcmp(lg, &TEST_LOGIT[t * L2_N], sizeof(lg)) != 0) {
            if (mismatch < 3) printf("MISMATCH anh %d\n", t);
            mismatch++;
        }
    }
    printf("Golden C: %d anh, accuracy = %.2f%%, khop bit voi Python: %s (%d sai)\n",
           N_TEST, 100.0 * correct / N_TEST, mismatch ? "KHONG" : "CO", mismatch);
    printf("Layer mode: L0=%s L1=%s L2=%s\n", L0_MODE ? "POT" : "INT8", L1_MODE ? "POT" : "INT8", L2_MODE ? "POT" : "INT8");
    return mismatch ? 1 : 0;
}
