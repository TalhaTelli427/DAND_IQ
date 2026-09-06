# ---------------------------------------------------------------------------
# Dand-IQ — Vivado project generator
#
#   vivado -mode batch -source fpga/scripts/create_project.tcl
#   vivado -mode gui   -source fpga/scripts/create_project.tcl
#
# Builds ./build/Dand_IQ.xpr from the sources in this repository: RTL,
# constraints and the design_1 block design, with design_1_wrapper as top.
# Nothing it generates is tracked by git.
#
# Target: Zynq-7020 (xc7z020clg400-1) — MYIR Z-Turn V2
# Tested with Vivado 2023.2
# ---------------------------------------------------------------------------

set script_dir [file normalize [file dirname [info script]]]
set repo_root  [file normalize $script_dir/../..]
set fpga_root  $repo_root/fpga

set proj_name  Dand_IQ
set proj_dir   $repo_root/build
set part       xc7z020clg400-1
set board      myir.com:mys-7z020:part0:2.1

file mkdir $proj_dir
create_project $proj_name $proj_dir -part $part -force

# Board file is optional — the design only needs the part.
if { [llength [get_board_parts -quiet $board]] } {
    set_property BOARD_PART $board [current_project]
} else {
    puts "INFO: board part $board not installed, continuing with part only."
}

set_property target_language VHDL [current_project]

# --- RTL --------------------------------------------------------------------
set rtl_files [glob -nocomplain \
    $fpga_root/rtl/uart/*.vhd \
    $fpga_root/rtl/framing/*.vhd \
    $fpga_root/rtl/modem/*.vhd \
    $fpga_root/rtl/common/*.vhd]

add_files -norecurse -fileset sources_1 $rtl_files
set_property file_type {VHDL} [get_files $rtl_files]

# --- Constraints ------------------------------------------------------------
add_files -norecurse -fileset constrs_1 [glob $fpga_root/constraints/*.xdc]

# --- Simulation (optional) --------------------------------------------------
set sim_files [glob -nocomplain $fpga_root/sim/*.vhd]
if { [llength $sim_files] } {
    add_files -norecurse -fileset sim_1 $sim_files
}

update_compile_order -fileset sources_1

# --- Block design -----------------------------------------------------------
# design_1.bd is referenced in place. Vivado writes its generated output
# (ip/, hdl/, sim/, synth/) next to it; .gitignore keeps that out of the repo.
set bd_file $fpga_root/bd/design_1/design_1.bd
add_files -norecurse -fileset sources_1 $bd_file

open_bd_design [get_files design_1.bd]

# Pin the RRC coefficient file to this checkout so the FIR IP always finds it.
set coe [file normalize $fpga_root/ip/rrc_0p35_4x_129tap.coe]
foreach cell [get_bd_cells -quiet -filter {VLNV =~ "xilinx.com:ip:fir_compiler:*"}] {
    set_property CONFIG.Coefficient_File $coe $cell
}
validate_bd_design -quiet
save_bd_design
close_bd_design [current_bd_design]

generate_target all [get_files design_1.bd]
make_wrapper -files [get_files design_1.bd] -top -import
set_property top design_1_wrapper [current_fileset]
update_compile_order -fileset sources_1

puts ""
puts "-----------------------------------------------------------"
puts " Project created: $proj_dir/$proj_name.xpr"
puts " Next: launch_runs impl_1 -to_step write_bitstream -jobs 8"
puts "-----------------------------------------------------------"
