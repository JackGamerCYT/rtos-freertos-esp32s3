"""
Sinh dữ liệu cho golden C và RTL từ một qmodel_*.npz.

  python export_hw.py --qmodel out/qmodel_IPI.npz --dataset mnist
  -> ../03_golden_c/model_params.h, ../03_golden_c/test_vectors.h
  -> ../05_rtl/sim/vectors/<case>/{cfg.txt,a.hex,w.hex,bias.hex,exp.hex}

Layout bộ nhớ trong NPU (phải khớp RTL và driver PS):
  A bank i  (i = m % ROWS): addr = (m // ROWS) * K + k
  W bank j  (j = n % COLS): addr = (n // COLS) * K + k
  OUT       : addr = m * N + n (32 bit)
"""
import argparse, os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qsim

ROWS, COLS = 8, 8
A_DEPTH, W_DEPTH, O_DEPTH, B_DEPTH = 2048, 2048, 8192, 64


def load_qmodel(path):
    d = np.load(path)
    layers = []
    for i in range(int(d["n"])):
        g = lambda k: d[f"L{i}_{k}"]
        layers.append(dict(type=str(g("type")), mode=int(g("mode")), w_int=g("w_int").astype(np.int32),
                           code=g("code").astype(np.uint8), bias=g("bias").astype(np.int64),
                           mult=int(g("mult")), shift=int(g("shift")), relu=bool(g("relu")), raw=bool(g("raw"))))
    return dict(img=int(d["img"]), layers=layers)


def test_images(dataset, n):
    if dataset == "digits":
        from sklearn.datasets import load_digits
        import torch
        dd = load_digits()
        idx = torch.randperm(len(dd.images), generator=torch.Generator().manual_seed(0)).numpy()
        te = idx[int(0.8 * len(idx)):][:n]
        x, y = dd.images[te] / 16.0, dd.target[te]
    else:
        from torchvision import datasets
        te = datasets.MNIST("data", train=False, download=True)
        x, y = te.data[:n].numpy() / 255.0, te.targets[:n].numpy()
    return np.clip(np.round(x * 127), 0, 127).astype(np.int32), y.astype(np.int32)


# ---------- golden C ----------
def c_array(name, ctype, arr, per_line=16):
    arr = np.asarray(arr).reshape(-1)
    lines = []
    for i in range(0, len(arr), per_line):
        lines.append("  " + ", ".join(str(int(v)) for v in arr[i:i + per_line]))
    return f"static const {ctype} {name}[{len(arr)}] = {{\n" + ",\n".join(lines) + "\n};\n"


def write_c(qm, imgs, labels, cdir):
    os.makedirs(cdir, exist_ok=True)
    s = ["// Sinh tự động bởi export_hw.py - không sửa tay", "#pragma once", "#include <stdint.h>",
         f"#define IMG {qm['img']}", f"#define N_LAYERS {len(qm['layers'])}", ""]
    for i, L in enumerate(qm["layers"]):
        K, N = L["w_int"].shape
        s.append(f"// layer {i}: {L['type']} K={K} N={N} mode={'POT' if L['mode'] else 'INT8'}")
        s.append(f"#define L{i}_K {K}\n#define L{i}_N {N}\n#define L{i}_MODE {L['mode']}")
        s.append(f"#define L{i}_MULT {L['mult']}\n#define L{i}_SHIFT {L['shift']}")
        s.append(f"#define L{i}_RELU {int(L['relu'])}\n#define L{i}_RAW {int(L['raw'])}")
        # trọng số lưu đúng như trong BRAM: byte int8 hoặc mã PoT 4 bit, layout [k][n]
        s.append(c_array(f"L{i}_W", "uint8_t", L["code"]))
        s.append(c_array(f"L{i}_B", "int32_t", L["bias"]))
    open(os.path.join(cdir, "model_params.h"), "w").write("\n".join(s))

    logits = np.stack([qsim.forward_int(im, qm) for im in imgs])
    t = ["// Sinh tự động bởi export_hw.py", "#pragma once", "#include <stdint.h>",
         f"#define N_TEST {len(imgs)}", c_array("TEST_IMG", "uint8_t", imgs),
         c_array("TEST_LABEL", "uint8_t", labels), c_array("TEST_LOGIT", "int32_t", logits)]
    open(os.path.join(cdir, "test_vectors.h"), "w").write("\n".join(t))
    acc = 100.0 * (logits.argmax(1) == labels).mean()
    print(f"golden C header: {len(imgs)} ảnh, integer acc = {acc:.2f}%")


# ---------- vector RTL ----------
def write_case(vdir, A, code, bias, mode, M_, S_, relu, raw, expected):
    os.makedirs(vdir, exist_ok=True)
    M, K = A.shape
    N = code.shape[1]
    assert -(-M // ROWS) * K <= A_DEPTH and -(-N // COLS) * K <= W_DEPTH
    assert M * N <= O_DEPTH and N <= B_DEPTH
    amem = np.zeros(ROWS * A_DEPTH, dtype=np.int64)
    for m in range(M):
        base = (m % ROWS) * A_DEPTH + (m // ROWS) * K
        amem[base:base + K] = A[m]
    wmem = np.zeros(COLS * W_DEPTH, dtype=np.int64)
    for n in range(N):
        base = (n % COLS) * W_DEPTH + (n // COLS) * K
        wmem[base:base + K] = code[:, n]
    hx = lambda a, w: "\n".join(f"{int(v) & ((1 << (4 * w)) - 1):0{w}x}" for v in a) + "\n"
    open(os.path.join(vdir, "a.hex"), "w").write(hx(amem, 2))
    open(os.path.join(vdir, "w.hex"), "w").write(hx(wmem, 2))
    open(os.path.join(vdir, "bias.hex"), "w").write(hx(np.pad(bias, (0, B_DEPTH - N)), 8))
    open(os.path.join(vdir, "exp.hex"), "w").write(hx(expected.reshape(-1), 8))
    open(os.path.join(vdir, "cfg.txt"), "w").write(f"{M} {N} {K} {mode} {int(relu)} {int(raw)} {M_} {S_}\n")


def random_case(rng, M, N, K, mode, raw):
    A = rng.integers(0, 128, size=(M, K))
    if mode == qsim.MODE_INT8:
        w = rng.integers(-127, 128, size=(K, N)); code = (w & 0xFF).astype(np.uint8)
    else:
        code = rng.integers(0, 16, size=(K, N)).astype(np.uint8); w = qsim.pot_decode(code)
    bias = rng.integers(-5000, 5000, size=N)
    Mq, Sq = qsim.quant_multiplier(0.0011)
    out = qsim.gemm_requant(A, w, bias, Mq, Sq, relu=not raw, raw=raw)
    return A, code, bias, Mq, Sq, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--qmodel", required=True)
    ap.add_argument("--dataset", default="mnist", choices=["mnist", "digits"])
    ap.add_argument("--ntest", type=int, default=100)
    args = ap.parse_args()
    root = os.path.dirname(HERE)
    qm = load_qmodel(args.qmodel)
    imgs, labels = test_images(args.dataset, args.ntest)
    write_c(qm, imgs, labels, os.path.join(root, "03_golden_c"))

    vroot = os.path.join(root, "05_rtl", "sim", "vectors")
    trace = []
    qsim.forward_int(imgs[0], qm, trace)
    for i, ((A, out), L) in enumerate(zip(trace, qm["layers"])):
        write_case(os.path.join(vroot, f"img0_L{i}"), A, L["code"], L["bias"], L["mode"],
                   L["mult"], L["shift"], L["relu"], L["raw"], out)
    rng = np.random.default_rng(1)
    for name, (M, N, K, mode, raw) in {"rand_int8": (13, 11, 5, 0, 0), "rand_pot": (17, 9, 23, 1, 0),
                                       "rand_raw": (3, 20, 40, 1, 1), "rand_big": (40, 16, 50, 0, 0),
                                       # đúng kích thước 3 layer của Tiny CNN trên MNIST 28x28
                                       "shape_mnist_L0": (784, 8, 9, 0, 0), "shape_mnist_L1": (196, 16, 72, 1, 0),
                                       "shape_mnist_L2": (1, 10, 784, 0, 1)}.items():
        A, code, bias, Mq, Sq, out = random_case(rng, M, N, K, mode, raw)
        write_case(os.path.join(vroot, name), A, code, bias, mode, Mq, Sq, not raw, raw, out)
    print("RTL vectors ->", vroot, sorted(os.listdir(vroot)))


if __name__ == "__main__":
    main()
