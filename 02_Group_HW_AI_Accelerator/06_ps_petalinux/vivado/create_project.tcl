# Tạo project Vivado + block design: Zynq PS + npu_axil (RTL module reference).
# Mặc định Ultra96-V2 (xczu3eg-sbva484-1-i). Đổi PART/BOARD cho kit khác (PYNQ-Z2: xc7z020clg400-1, KV260: xck26-sfvc784-2LV-c).
# Chạy:  vivado -mode batch -source create_project.tcl        (CHƯA chạy thử - cần Vivado 2022.2 trở lên)
set PART  xczu3eg-sbva484-1-i
set BOARD avnet.com:ultra96v2:part0:1.2
set ROOT  [file normalize [file dirname [info script]]/../..]

create_project npu_soc ./npu_soc -part $PART -force
catch { set_property board_part $BOARD [current_project] }
add_files [glob $ROOT/05_rtl/src/*.v]
set_property top npu_axil [current_fileset]
update_compile_order -fileset sources_1

create_bd_design "system"
set ps [create_bd_cell -type ip -vlnv xilinx.com:ip:zynq_ultra_ps_e zynq_ps]
catch { apply_bd_automation -rule xilinx.com:bd_rule:zynq_ultra_ps_e -config {apply_board_preset "1"} $ps }
# 1 cổng master HPM0_FPD, clock PL0 = 100 MHz, bật ngắt PL->PS
set_property -dict [list CONFIG.PSU__USE__M_AXI_GP0 {1} CONFIG.PSU__USE__M_AXI_GP1 {0} CONFIG.PSU__USE__M_AXI_GP2 {0} \
    CONFIG.PSU__FPGA_PL0_ENABLE {1} CONFIG.PSU__CRL_APB__PL0_REF_CTRL__FREQMHZ {100} CONFIG.PSU__USE__IRQ0 {1}] $ps

create_bd_cell -type module -reference npu_axil npu_0
apply_bd_automation -rule xilinx.com:bd_rule:axi4 -config { Clk_master {Auto} Clk_slave {Auto} Clk_xbar {Auto} \
    Master {/zynq_ps/M_AXI_HPM0_FPD} Slave {/npu_0/s_axi} intc_ip {New AXI SmartConnect} master_apm {0}} [get_bd_intf_pins npu_0/s_axi]
connect_bd_net [get_bd_pins npu_0/irq] [get_bd_pins zynq_ps/pl_ps_irq0]
set_property range 1M [get_bd_addr_segs {zynq_ps/Data/SEG_npu_0_reg0}]
set_property offset 0xA0000000 [get_bd_addr_segs {zynq_ps/Data/SEG_npu_0_reg0}]
validate_bd_design
save_bd_design

make_wrapper -files [get_files system.bd] -top
add_files -norecurse ./npu_soc/npu_soc.gen/sources_1/bd/system/hdl/system_wrapper.v
set_property top system_wrapper [current_fileset]
launch_runs impl_1 -to_step write_bitstream -jobs 4
wait_on_run impl_1
write_hw_platform -fixed -include_bit -force ./system_wrapper.xsa
puts "XSA -> ./system_wrapper.xsa (dùng cho petalinux-config --get-hw-description)"
