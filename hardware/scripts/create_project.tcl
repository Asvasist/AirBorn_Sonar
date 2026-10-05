# Recreate the Vivado project from version-controlled sources.
#
#   vivado -mode batch -source hardware/scripts/create_project.tcl
#
# The project is written to hardware/vivado/ (git-ignored).

set hw_dir   [file normalize [file join [file dirname [info script]] ..]]
set proj_dir [file join $hw_dir vivado]

create_project airborne_sonar $proj_dir -part xc7z020clg400-1 -force
set_property target_language Verilog [current_project]

add_files -norecurse [glob [file join $hw_dir rtl *.v]]
add_files -fileset constrs_1 -norecurse [file join $hw_dir constraints zybo_z7_sonar.xdc]
update_compile_order -fileset sources_1

# The block design instantiates the RTL above as module references.
source [file join $hw_dir bd pdm_dma_bd.tcl]
validate_bd_design
save_bd_design

set bd_file [get_files pdm_dma_bd.bd]
add_files -norecurse [make_wrapper -files $bd_file -top]
set_property top pdm_dma_bd_wrapper [current_fileset]
update_compile_order -fileset sources_1
