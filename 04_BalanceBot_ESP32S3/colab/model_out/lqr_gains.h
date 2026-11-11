// LQR danh định (dự phòng): u = -(K0*theta + K1*theta_dot + K2*phi_err + K3*phi_dot_err + K4*u_prev)
#pragma once
static const float LQR_K[5] = {
  -7.07337987e+00f, -4.32573282e-01f, -1.52998601e-02f, -1.01948772e-01f, 5.76996444e-01f
};
