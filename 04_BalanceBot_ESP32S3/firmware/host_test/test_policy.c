// gcc -O2 -Iinclude host_test/test_policy.c src/policy.c -lm && ./a.out
#include <stdio.h>
#include <math.h>
#include "policy.h"
#include "balance_policy.h"
#include "balance_policy_test.h"
int main(void) {
    float worst = 0;
    for (int t = 0; t < BP_NTEST; t++) {
        float y = policy_run(&BP_TEST_X[t * BP_IN]);
        float e = fabsf(y - BP_TEST_Y[t]);
        if (e > worst) worst = e;
    }
    printf("%d vector, sai lech lon nhat C vs Python = %.2e -> %s\n", BP_NTEST, worst, worst < 1e-5 ? "PASS" : "FAIL");
    return worst < 1e-5 ? 0 : 1;
}
