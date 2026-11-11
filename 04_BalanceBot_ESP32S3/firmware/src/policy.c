#include <math.h>
#include "policy.h"
#include "balance_policy.h"

static void dense_tanh(const float *in, int n_in, const float *W, const float *b, int n_out, float *out) {
    for (int j = 0; j < n_out; j++) out[j] = b[j];
    for (int i = 0; i < n_in; i++) {
        const float xi = in[i];
        const float *w = &W[i * n_out];
        for (int j = 0; j < n_out; j++) out[j] += xi * w[j];
    }
    for (int j = 0; j < n_out; j++) out[j] = tanhf(out[j]);
}

float policy_run(const float *x) {
    float h1[BP_H1], h2[BP_H2], y;
    dense_tanh(x, BP_IN, BP_W1, BP_B1, BP_H1, h1);
    dense_tanh(h1, BP_H1, BP_W2, BP_B2, BP_H2, h2);
    dense_tanh(h2, BP_H2, BP_W3, BP_B3, 1, &y);
    return y;
}
