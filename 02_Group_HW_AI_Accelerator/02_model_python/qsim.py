"""
Mô hình số nguyên (integer reference) - phải khớp bit với golden C và RTL.

Quy ước:
  activation : int8 trong [0,127] (sau ReLU), zero-point = 0, scale s_a
  weight INT8: int8 [-127,127], scale s_w
  weight POT : mã 4 bit = {sign[3], e[2:0]}, giá trị = ±2^e (e=0..6), e=7 -> 0
  acc        : int32 = sum(a*w) + bias
  requant    : y = (acc*M + 2^(S-1)) >> S  (dịch số học), ReLU, kẹp [0,127]
  raw mode   : xuất thẳng acc int32 (layer cuối, để argmax)
"""
import numpy as np

MODE_INT8, MODE_POT = 0, 1
POT_ZERO = 7
POT_EMAX = 6


# ---------- lượng tử trọng số ----------
def quant_int8(w, clip):
    s = max(clip, 1e-12) / 127.0
    q = np.clip(np.round(w / s), -127, 127).astype(np.int32)
    return q, s


def quant_pot(w, clip):
    s = max(clip, 1e-12) / 2.0 ** POT_EMAX
    m = np.abs(w) / s
    e = np.clip(np.round(np.log2(np.maximum(m, 1e-12))), 0, POT_EMAX).astype(np.int32)
    zero = m < 0.5
    val = np.where(zero, 0, (1 << e)) * np.where(w < 0, -1, 1)
    code = np.where(zero, POT_ZERO, e) | np.where((w < 0) & ~zero, 8, 0)
    return val.astype(np.int32), code.astype(np.uint8), s


def quant_weight(w, mode):
    """Tìm ngưỡng clip theo MSE (lưới 20 điểm). Trả (w_int, code_8bit, scale)."""
    wmax = float(np.abs(w).max())
    best = None
    for r in np.linspace(0.4, 1.0, 20):
        if mode == MODE_INT8:
            q, s = quant_int8(w, wmax * r)
            code = (q & 0xFF).astype(np.uint8)
        else:
            q, code, s = quant_pot(w, wmax * r)
        err = float(((q * s - w) ** 2).sum())
        if best is None or err < best[0]:
            best = (err, q, code, s)
    return best[1], best[2], best[3]


def pot_decode(code):
    code = np.asarray(code, dtype=np.int32)
    e = code & 7
    mag = np.where(e == POT_ZERO, 0, 1 << np.minimum(e, POT_EMAX))
    return np.where(code & 8, -mag, mag).astype(np.int32)


def quant_multiplier(real):
    """real ~= M / 2^S với M 15 bit (16384..32767)."""
    assert 0 < real < 1.0
    S = 0
    while real * (1 << S) < 16384:
        S += 1
    M = int(round(real * (1 << S)))
    if M == 32768:
        M //= 2
        S -= 1
    return M, S


# ---------- các phép toán ----------
def im2col(a_chw, k=3, pad=1):
    C, H, W = a_chw.shape
    p = np.pad(a_chw, ((0, 0), (pad, pad), (pad, pad)))
    cols = np.zeros((H * W, C * k * k), dtype=np.int32)
    idx = 0
    for c in range(C):
        for ky in range(k):
            for kx in range(k):
                cols[:, idx] = p[c, ky:ky + H, kx:kx + W].reshape(-1)
                idx += 1
    return cols            # hàng m = y*W + x ; cột K = (c*3+ky)*3+kx


def gemm_requant(A, Wkn, bias, M, S, relu=True, raw=False):
    acc = A.astype(np.int64) @ Wkn.astype(np.int64) + bias.astype(np.int64)
    assert np.all(np.abs(acc) < 2 ** 31), "tràn int32"
    if raw:
        return acc.astype(np.int64)
    y = (acc * M + (1 << (S - 1))) >> S
    lo = 0 if relu else -128
    return np.clip(y, lo, 127).astype(np.int64)


def maxpool2(a_chw):
    C, H, W = a_chw.shape
    return a_chw.reshape(C, H // 2, 2, W // 2, 2).max(axis=(2, 4))


def forward_int(img_u8, qm, trace=None):
    """img_u8: (H,W) int trong [0,127]. qm: dict từ export. trace: list nhận (A, out) từng layer."""
    a = img_u8[None].astype(np.int32)
    for L in qm["layers"]:
        if L["type"] == "conv":
            C, H, W = a.shape
            A = im2col(a)
        else:
            A = a.reshape(1, -1)
        Wkn = L["w_int"]
        out = gemm_requant(A, Wkn, L["bias"], L["mult"], L["shift"], L["relu"], L["raw"])
        if trace is not None:
            trace.append((A, out))
        if L["type"] == "conv":
            a = out.T.reshape(-1, H, W)         # (N, H, W)
            if L.get("pool", True):
                a = maxpool2(a)
        else:
            a = out.reshape(-1)
    return a
