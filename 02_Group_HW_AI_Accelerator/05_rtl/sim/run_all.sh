#!/usr/bin/env bash
# Mô phỏng toàn bộ bằng Icarus Verilog:  bash run_all.sh
set -e
cd "$(dirname "$0")"
SRC="../src/npu_pe.v ../src/npu_delay.v ../src/npu_array.v ../src/npu_requant.v ../src/npu_core.v ../src/npu_axil.v"
iverilog -g2012 -o tb_pe.vvp ../src/npu_pe.v ../tb/tb_pe.v && vvp -n tb_pe.vvp | grep tb_pe
iverilog -g2012 -o tb_npu.vvp $SRC ../tb/tb_npu_axil.v
for d in vectors/*/; do
    printf "%-12s " "$(basename "$d")"
    (cd "$d" && vvp -n ../../tb_npu.vvp | grep -E "RESULT|SAI|TIMEOUT")
done
