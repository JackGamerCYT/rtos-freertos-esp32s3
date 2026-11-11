#include "npu_drv.h"
#include <stdio.h>
#include <string.h>

#ifndef NPU_EMU
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

static volatile uint32_t *regs;
static int fd = -1;

int npu_open(const char *dev, uint64_t phys_base) {
    fd = open(dev, O_RDWR | O_SYNC);
    if (fd < 0) { perror(dev); return -1; }
    off_t off = strncmp(dev, "/dev/uio", 8) == 0 ? 0 : (off_t)phys_base;
    void *p = mmap(NULL, NPU_SPAN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, off);
    if (p == MAP_FAILED) { perror("mmap"); close(fd); return -1; }
    regs = (volatile uint32_t *)p;
    return 0;
}
void npu_close(void) { munmap((void *)regs, NPU_SPAN); close(fd); }
uint32_t npu_rd(uint32_t off) { return regs[off >> 2]; }
void npu_wr(uint32_t off, uint32_t v) { regs[off >> 2] = v; }

#else
// ---------- giả lập phần cứng trên PC (hành vi theo npu_core.v) ----------
#include "../03_golden_c/npu_ref.h"
static uint32_t csr[16];
static int32_t  bias_m[64], out_m[NPU_OUT_D];
static uint8_t  a_m[NPU_ROWS][NPU_BANK_D], w_m[NPU_COLS][NPU_BANK_D];

int npu_open(const char *dev, uint64_t b) { (void)dev; (void)b; return 0; }
void npu_close(void) {}
static void emu_run(void) {
    int M = csr[2], N = csr[3], K = csr[4], cfg = csr[5];
    for (int m = 0; m < M; m++)
        for (int n = 0; n < N; n++) {
            int64_t acc = 0;
            for (int k = 0; k < K; k++) {
                int32_t a = (int8_t)a_m[m % NPU_ROWS][(m / NPU_ROWS) * K + k];
                acc += npu_pe_mul(a, w_m[n % NPU_COLS][(n / NPU_COLS) * K + k], cfg & 1);
            }
            acc += bias_m[n];
            out_m[m * N + n] = (cfg & 4) ? (int32_t)acc : npu_requant(acc, csr[6], csr[7], (cfg >> 1) & 1);
        }
    csr[1] = 2;
}
uint32_t npu_rd(uint32_t off) {
    if (off >= NPU_OBUF) return out_m[(off - NPU_OBUF) >> 2];
    if (off >= NPU_BIAS && off < NPU_ABUF) return bias_m[(off - NPU_BIAS) >> 2];
    return csr[(off >> 2) & 15];
}
void npu_wr(uint32_t off, uint32_t v) {
    if (off >= NPU_WBUF && off < NPU_OBUF) { uint32_t i = (off - NPU_WBUF) >> 2; w_m[i / NPU_BANK_D][i % NPU_BANK_D] = v; }
    else if (off >= NPU_ABUF) { uint32_t i = (off - NPU_ABUF) >> 2; a_m[i / NPU_BANK_D][i % NPU_BANK_D] = v; }
    else if (off >= NPU_BIAS) bias_m[(off - NPU_BIAS) >> 2] = v;
    else if (off == NPU_CTRL) { if (v & 1) emu_run(); }
    else csr[off >> 2] = v;
}
#endif

// ---------- phần chung: nạp dữ liệu theo layout bank rồi chạy ----------
int npu_gemm(const int32_t *A, const uint8_t *W, const int32_t *bias,
             const npu_layer_cfg *c, int32_t *out, uint32_t *cycles) {
    int M = c->M, N = c->N, K = c->K;
    if (((M + NPU_ROWS - 1) / NPU_ROWS) * K > NPU_BANK_D ||
        ((N + NPU_COLS - 1) / NPU_COLS) * K > NPU_BANK_D || M * N > NPU_OUT_D || N > 64) {
        fprintf(stderr, "layer vuot kich thuoc buffer (M=%d N=%d K=%d)\n", M, N, K);
        return -1;
    }
    for (int m = 0; m < M; m++) {
        uint32_t base = NPU_ABUF + ((m % NPU_ROWS) * NPU_BANK_D + (m / NPU_ROWS) * K) * 4;
        for (int k = 0; k < K; k++) npu_wr(base + k * 4, (uint32_t)(A[m * K + k] & 0xFF));
    }
    for (int n = 0; n < N; n++) {
        uint32_t base = NPU_WBUF + ((n % NPU_COLS) * NPU_BANK_D + (n / NPU_COLS) * K) * 4;
        for (int k = 0; k < K; k++) npu_wr(base + k * 4, W[k * N + n]);
    }
    for (int n = 0; n < N; n++) npu_wr(NPU_BIAS + n * 4, (uint32_t)bias[n]);
    npu_wr(NPU_M, M); npu_wr(NPU_N, N); npu_wr(NPU_K, K);
    npu_wr(NPU_CFG, (c->mode & 1) | ((c->relu & 1) << 1) | ((c->raw & 1) << 2));
    npu_wr(NPU_MULT, (uint32_t)c->mult); npu_wr(NPU_SHIFT, (uint32_t)c->shift);
    npu_wr(NPU_CTRL, 1);
    long spin = 0;
    while (!(npu_rd(NPU_STATUS) & 2))
        if (++spin > 100000000L) { fprintf(stderr, "NPU timeout\n"); return -2; }
    if (cycles) *cycles = npu_rd(NPU_CYCLES);
    for (int i = 0; i < M * N; i++) out[i] = (int32_t)npu_rd(NPU_OBUF + i * 4);
    return 0;
}
