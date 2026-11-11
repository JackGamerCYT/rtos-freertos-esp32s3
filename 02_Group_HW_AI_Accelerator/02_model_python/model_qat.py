# %% [markdown]
# # Tiny CNN + lượng tử hỗn hợp INT8 / Power-of-Two (PoT) theo từng layer
# Chạy trên Colab: dán từng cell (mỗi khối `# %%`) hoặc upload cả thư mục rồi `!python model_qat.py`.
# Chạy local (không GPU, không tải MNIST được): `python model_qat.py --dataset digits`
#
# Đầu ra (thư mục out/):
#   qmodel_<cfg>.npz      : model số nguyên cho từng cấu hình
#   results.json          : accuracy FP32 / fake-quant / integer cho 8 cấu hình
#   chosen.json           : cấu hình được chọn theo ràng buộc accuracy + chi phí
#   (sau đó chạy export_hw.py để sinh header cho golden C và vector test cho RTL)

# %%
import argparse, itertools, json, os, sys, time
import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else ".")
import qsim

p = argparse.ArgumentParser()
p.add_argument("--dataset", default="mnist", choices=["mnist", "digits"])
p.add_argument("--epochs", type=int, default=4)
p.add_argument("--qat_epochs", type=int, default=1)
p.add_argument("--tol", type=float, default=0.5, help="cho phép giảm accuracy (%) so với all-INT8")
p.add_argument("--out", default="out")
args, _ = p.parse_known_args()
torch.manual_seed(0); np.random.seed(0)
dev = "cuda" if torch.cuda.is_available() else "cpu"
os.makedirs(args.out, exist_ok=True)
print("device:", dev, "| dataset:", args.dataset)

# %% Dữ liệu
def load_data(name):
    if name == "mnist":
        from torchvision import datasets, transforms
        tf = transforms.ToTensor()                      # [0,1], không chuẩn hoá để giữ zero-point = 0
        tr = datasets.MNIST("data", train=True, download=True, transform=tf)
        te = datasets.MNIST("data", train=False, download=True, transform=tf)
        xtr = tr.data.float().div(255).unsqueeze(1); ytr = tr.targets
        xte = te.data.float().div(255).unsqueeze(1); yte = te.targets
    else:
        from sklearn.datasets import load_digits
        d = load_digits()
        x = torch.tensor(d.images, dtype=torch.float32).div(16).unsqueeze(1)
        y = torch.tensor(d.target)
        idx = torch.randperm(len(x), generator=torch.Generator().manual_seed(0))
        n = int(0.8 * len(x))
        xtr, ytr, xte, yte = x[idx[:n]], y[idx[:n]], x[idx[n:]], y[idx[n:]]
    return xtr, ytr, xte, yte

xtr, ytr, xte, yte = load_data(args.dataset)
IMG = xtr.shape[-1]
print("train", tuple(xtr.shape), "test", tuple(xte.shape))

def batches(x, y, bs, shuffle):
    idx = torch.randperm(len(x)) if shuffle else torch.arange(len(x))
    for i in range(0, len(x), bs):
        j = idx[i:i + bs]
        yield x[j].to(dev), y[j].to(dev)

# %% Fake-quant (giống hệt qsim, có STE)
def fq_weight(w, mode):
    wmax = w.detach().abs().max()
    best_err, best = None, None
    for r in torch.linspace(0.4, 1.0, 20, device=w.device):
        clip = wmax * r
        if mode == qsim.MODE_INT8:
            s = clip / 127
            q = torch.clamp(torch.round(w.detach() / s), -127, 127) * s
        else:
            s = clip / 2 ** qsim.POT_EMAX
            m = w.detach().abs() / s
            e = torch.clamp(torch.round(torch.log2(m.clamp_min(1e-12))), 0, qsim.POT_EMAX)
            q = torch.where(m < 0.5, torch.zeros_like(m), 2 ** e) * torch.sign(w.detach()) * s
        err = ((q - w.detach()) ** 2).sum()
        if best_err is None or err < best_err:
            best_err, best = err, q
    return w + (best - w).detach()

def fq_act(x, s):
    return x + (torch.clamp(torch.round(x / s), 0, 127) * s - x).detach()

class TinyCNN(nn.Module):
    def __init__(self, img):
        super().__init__()
        self.c1 = nn.Conv2d(1, 8, 3, padding=1)
        self.c2 = nn.Conv2d(8, 16, 3, padding=1)
        self.fc = nn.Linear(16 * (img // 4) ** 2, 10)
        self.modes = None          # None = FP32, ngược lại tuple 3 mode
        self.act_s = None          # scale activation đầu ra c1, c2

    def forward(self, x):
        q = self.modes is not None
        if q:
            x = fq_act(x, 1 / 127)
        w = fq_weight(self.c1.weight, self.modes[0]) if q else self.c1.weight
        x = F.relu(F.conv2d(x, w, self.c1.bias, padding=1))
        if q: x = fq_act(x, self.act_s[0])
        x = F.max_pool2d(x, 2)
        w = fq_weight(self.c2.weight, self.modes[1]) if q else self.c2.weight
        x = F.relu(F.conv2d(x, w, self.c2.bias, padding=1))
        if q: x = fq_act(x, self.act_s[1])
        x = F.max_pool2d(x, 2).flatten(1)
        w = fq_weight(self.fc.weight, self.modes[2]) if q else self.fc.weight
        return F.linear(x, w, self.fc.bias)

def evaluate(model):
    model.eval(); ok = 0
    with torch.no_grad():
        for xb, yb in batches(xte, yte, 1000, False):
            ok += (model(xb).argmax(1) == yb).sum().item()
    return 100.0 * ok / len(xte)

def train(model, epochs, lr):
    opt = torch.optim.Adam(model.parameters(), lr=lr)
    for ep in range(epochs):
        model.train()
        for xb, yb in batches(xtr, ytr, 128, True):
            opt.zero_grad()
            F.cross_entropy(model(xb), yb).backward()
            opt.step()
        print(f"  epoch {ep + 1}/{epochs} acc={evaluate(model):.2f}%")

# %% Train FP32
fp = TinyCNN(IMG).to(dev)
ep = args.epochs if args.dataset == "mnist" else max(args.epochs, 30)
t0 = time.time(); train(fp, ep, 1e-3)
acc_fp = evaluate(fp)
print(f"FP32 acc = {acc_fp:.2f}%  ({time.time() - t0:.0f}s)")

# %% Calibrate scale activation (percentile 99.99 trên 2000 ảnh train)
with torch.no_grad():
    xb = xtr[:2000].to(dev)
    a1 = F.relu(fp.c1(xb))
    a2 = F.relu(fp.c2(F.max_pool2d(a1, 2)))
    act_s = [float(torch.quantile(a.flatten()[::7], 0.9999)) / 127 for a in (a1, a2)]
print("act scales:", act_s)

# %% Chuyển sang model số nguyên (dùng chung qsim)
def to_int_model(model, modes):
    s_in = 1 / 127
    out_s = act_s + [None]
    layers = []
    for li, (mod, typ) in enumerate([(model.c1, "conv"), (model.c2, "conv"), (model.fc, "fc")]):
        w = mod.weight.detach().cpu().numpy().astype(np.float64)
        b = mod.bias.detach().cpu().numpy().astype(np.float64)
        w2 = w.reshape(w.shape[0], -1)                       # (N, K)
        q, code, s_w = qsim.quant_weight(w2, modes[li])
        bias = np.round(b / (s_in * s_w)).astype(np.int64)
        raw = out_s[li] is None
        if raw:
            M, S = 1, 1
        else:
            M, S = qsim.quant_multiplier(s_in * s_w / out_s[li])
        layers.append(dict(type=typ, mode=int(modes[li]), w_int=q.T.copy(), code=code.T.copy(),
                           bias=bias, mult=M, shift=S, relu=not raw, raw=raw,
                           s_w=s_w, s_in=s_in, s_out=out_s[li]))
        if not raw:
            s_in = out_s[li]
    return dict(img=IMG, layers=layers)

def eval_int(qm, n=None):
    x = xte if n is None else xte[:n]
    y = yte if n is None else yte[:n]
    img = np.clip(np.round(x.numpy()[:, 0] * 127), 0, 127).astype(np.int32)
    pred = np.array([int(np.argmax(qsim.forward_int(im, qm))) for im in img])
    return 100.0 * (pred == y.numpy()).mean()

def save_qmodel(qm, path):
    d = {"img": qm["img"], "n": len(qm["layers"])}
    for i, L in enumerate(qm["layers"]):
        for k, v in L.items():
            d[f"L{i}_{k}"] = np.asarray(v if v is not None else -1)
    np.savez(path, **d)

# %% Đánh giá 8 cấu hình (mỗi layer INT8 hoặc PoT) + QAT ngắn
n_params = [fp.c1.weight.numel(), fp.c2.weight.numel(), fp.fc.weight.numel()]
n_macs = [IMG * IMG * 9 * 8, (IMG // 2) ** 2 * 72 * 16, fp.fc.weight.numel()]
E_INT8, E_POT = 1.0, 0.3   # giả định năng lượng tương đối 1 MAC, cần đo lại bằng Vivado power report

results = []
for modes in itertools.product([0, 1], repeat=3):
    tag = "".join("P" if m else "I" for m in modes)
    m = TinyCNN(IMG).to(dev); m.load_state_dict(fp.state_dict())
    m.modes, m.act_s = modes, act_s
    acc_ptq = evaluate(m)
    if args.qat_epochs > 0:
        train(m, args.qat_epochs if args.dataset == "mnist" else 5, 2e-4)
    acc_fq = evaluate(m)
    qm = to_int_model(m, modes)
    acc_int = eval_int(qm)
    save_qmodel(qm, os.path.join(args.out, f"qmodel_{tag}.npz"))
    wbits = sum(n * (4 if md else 8) for n, md in zip(n_params, modes))
    energy = sum(n * (E_POT if md else E_INT8) for n, md in zip(n_macs, modes))
    r = dict(cfg=tag, ptq=acc_ptq, qat=acc_fq, int_acc=acc_int, weight_bits=wbits, mac_energy=energy)
    results.append(r)
    print(r)

# %% Chọn cấu hình: chi phí nhỏ nhất trong khi int_acc >= all-INT8 - tol
base = next(r for r in results if r["cfg"] == "III")["int_acc"]
ok = [r for r in results if r["int_acc"] >= base - args.tol]
e_max = max(r["mac_energy"] for r in results); b_max = max(r["weight_bits"] for r in results)
chosen = min(ok, key=lambda r: 0.5 * r["weight_bits"] / b_max + 0.5 * r["mac_energy"] / e_max)
print("\nFP32 =", acc_fp, "| all-INT8 =", base, "| chọn:", chosen)
json.dump(dict(fp32=acc_fp, act_scales=act_s, results=results, dataset=args.dataset,
               n_params=n_params, n_macs=n_macs), open(os.path.join(args.out, "results.json"), "w"), indent=2)
json.dump(chosen, open(os.path.join(args.out, "chosen.json"), "w"), indent=2)
torch.save(fp.state_dict(), os.path.join(args.out, "fp32.pt"))

# %% Bảng tóm tắt
print(f"{'cfg':5} {'PTQ':>7} {'QAT':>7} {'INT':>7} {'Wbits':>8} {'E_mac':>10}")
for r in results:
    print(f"{r['cfg']:5} {r['ptq']:7.2f} {r['qat']:7.2f} {r['int_acc']:7.2f} {r['weight_bits']:8d} {r['mac_energy']:10.0f}")
