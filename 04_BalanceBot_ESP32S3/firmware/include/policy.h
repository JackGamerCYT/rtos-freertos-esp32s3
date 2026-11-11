#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// Chạy MLP: x[BP_IN] đã chuẩn hoá -> lệnh duty u trong [-1, 1]
float policy_run(const float *x);
#ifdef __cplusplus
}
#endif
