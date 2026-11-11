#include "npu_ref.h"

void npu_gemm_ref(const int32_t *A, int M, int K, const uint8_t *W, int N, int mode,
                  const int32_t *bias, int32_t mult, int shift, int relu, int raw, int32_t *out) {
    for (int m = 0; m < M; m++)
        for (int n = 0; n < N; n++) {
            int64_t acc = bias[n];
            for (int k = 0; k < K; k++)
                acc += npu_pe_mul(A[m * K + k], W[k * N + n], mode);
            out[m * N + n] = raw ? (int32_t)acc : npu_requant(acc, mult, shift, relu);
        }
}

// cols[(y*W+x)*K + (c*3+ky)*3+kx], padding = 1, K = C*9
void npu_im2col3x3(const int32_t *in, int C, int H, int Wd, int32_t *cols) {
    int K = C * 9;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < Wd; x++) {
            int32_t *row = cols + (y * Wd + x) * K;
            for (int c = 0; c < C; c++)
                for (int ky = 0; ky < 3; ky++)
                    for (int kx = 0; kx < 3; kx++) {
                        int yy = y + ky - 1, xx = x + kx - 1;
                        int v = (yy < 0 || yy >= H || xx < 0 || xx >= Wd) ? 0 : in[(c * H + yy) * Wd + xx];
                        row[(c * 3 + ky) * 3 + kx] = v;
                    }
        }
}

void npu_maxpool2(const int32_t *in, int C, int H, int Wd, int32_t *out) {
    int Ho = H / 2, Wo = Wd / 2;
    for (int c = 0; c < C; c++)
        for (int y = 0; y < Ho; y++)
            for (int x = 0; x < Wo; x++) {
                const int32_t *p = in + (c * H + 2 * y) * Wd + 2 * x;
                int32_t m = p[0];
                if (p[1] > m) m = p[1];
                if (p[Wd] > m) m = p[Wd];
                if (p[Wd + 1] > m) m = p[Wd + 1];
                out[(c * Ho + y) * Wo + x] = m;
            }
}
