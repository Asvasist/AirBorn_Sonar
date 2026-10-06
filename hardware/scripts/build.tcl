# Synthesize, implement and export the hardware platform (XSA with bitstream).
#
#   vivado -mode batch -source hardware/scripts/build.tcl
#
# Output: hardware/vivado/airborne_sonar.xsa

set hw_dir [file normalize [file join [file dirname [info script]] ..]]
source [file join $hw_dir scripts create_project.tcl]

launch_runs impl_1 -to_step write_bitstream -jobs 8
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {
    error "Implementation failed; see hardware/vivado/airborne_sonar.runs/impl_1."
}

open_run impl_1
report_timing_summary -file [file join $hw_dir vivado timing_summary.rpt]
if {[get_property SLACK [get_timing_paths -max_paths 1 -setup]] < 0} {
    error "Design does not meet setup timing; see timing_summary.rpt."
}

write_hw_platform -fixed -include_bit -force [file join $hw_dir vivado airborne_sonar.xsa]
