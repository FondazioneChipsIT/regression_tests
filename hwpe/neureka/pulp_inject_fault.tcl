# Copyright 2023 ETH Zurich and University of Bologna.
# Solderpad Hardware License, Version 0.51, see LICENSE for details.
# SPDX-License-Identifier: SHL-0.51
#
# Author: Michael Rogenmoser (michaero@iis.ee.ethz.ch)

transcript quietly
if {! [info exists ::env(VSIM_PATH)]} {error "Define VSIM_PATH"}
set utils_base_path  [file join $::env(VSIM_PATH) scripts fault_injection_utils]
set script_base_path [file join $::env(VSIM_PATH) fault_injection_sim scripts]

set verbosity            2
set log_injections       1
# Easy way to generate a variable seed
# set seed                 [clock seconds]
# Default value
set seed                 12345
set print_statistics     1

set inject_start_time  75480ns
set inject_stop_time  75500ns
set injection_clock "pulp_cluster_tb/cluster_i/clk_i"
set injection_clock_trigger 0
set fault_period 5
set rand_initial_injection_phase 1
# max_num set to 0 means until stop_time
set max_num_fault_inject 0
set signal_fault_duration 20ns
set register_fault_duration 0ns

set allow_multi_bit_upset $::env(MULTI_BIT_UPSET)
set use_bitwidth_as_weight 0
set check_core_output_modification 0
set check_core_next_state_modification 0
set reg_to_sig_ratio 1

source [file join $utils_base_path pulp_extract_nets.tcl]

set inject_signals_netlist []
set inject_register_netlist []
set output_netlist []
set next_state_netlist []
set assertion_disable_list []

proc fault_injection_test_signals {} {
  set intc_list [list]
  lappend intc_list \
    "pulp_cluster_tb/cluster_i/hwpe_gen/hwpe_subsystem_i/gen_hwpe\[0\]/gen_neureka/i_neureka/i_engine/ft_datapath_gen/redundancy_gen\[0\]/i_double_infeat_buffer/i_even_infeat_buffer/i_infeat_buffer_scm/neureka_infeat_buffer_scm_i/buffer\[0\]" \
    "pulp_cluster_tb/cluster_i/hwpe_gen/hwpe_subsystem_i/gen_hwpe\[0\]/gen_neureka/i_neureka/i_engine/ft_datapath_gen/redundancy_gen\[0\]/i_double_infeat_buffer/i_even_infeat_buffer/i_infeat_buffer_scm/neureka_infeat_buffer_scm_i/buffer\[1\]"
  return $intc_list
}

set inject_register_netlist [list {*}$inject_register_netlist {*}[fault_injection_test_signals]]

source [file join $script_base_path inject_fault.tcl]

