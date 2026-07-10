# System Clock - 200 MHz Differential (Reference AC701 Board)
set_property -dict {PACKAGE_PIN R3 IOSTANDARD DIFF_SSTL15} [get_ports sys_clk_p]
set_property -dict {PACKAGE_PIN P3 IOSTANDARD DIFF_SSTL15} [get_ports sys_clk_n]
create_clock -period 5.000 -name sys_clk_pin [get_ports sys_clk_p]

# System Reset (Active-High Pushbutton)
set_property -dict {PACKAGE_PIN U14 IOSTANDARD LVCMOS15} [get_ports sys_rst]

# Diagnostic status LEDs
set_property -dict {PACKAGE_PIN T14 IOSTANDARD LVCMOS15} [get_ports led_link_a_up]
set_property -dict {PACKAGE_PIN T15 IOSTANDARD LVCMOS15} [get_ports led_link_b_up]
set_property -dict {PACKAGE_PIN T16 IOSTANDARD LVCMOS15} [get_ports led_activity_a]
set_property -dict {PACKAGE_PIN U16 IOSTANDARD LVCMOS15} [get_ports led_activity_b]

# SATA GTP Gigabit Transceiver Placements (Artix-7 GTP Quad 216)
# Port A uses GTPE2_CHANNEL_X0Y0, Port B uses GTPE2_CHANNEL_X0Y1
set_property LOC GTPE2_CHANNEL_X0Y0 [get_cells -hierarchical -filter {NAME =~ *gtp_port_a*}]
set_property LOC GTPE2_CHANNEL_X0Y1 [get_cells -hierarchical -filter {NAME =~ *gtp_port_b*}]
