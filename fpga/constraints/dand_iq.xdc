create_clock -period 27.778 -name ext_clk -waveform {0.000 13.889} [get_ports ext_clk]
set_property IOSTANDARD LVCMOS33 [get_ports ext_clk]

set_property IOSTANDARD LVCMOS33 [get_ports I_out]
set_property IOSTANDARD LVCMOS33 [get_ports Q_out]
set_property PACKAGE_PIN Y7 [get_ports I_out]
set_property PACKAGE_PIN Y9 [get_ports Q_out]

set_property PACKAGE_PIN T9 [get_ports ext_clk]

set_property PACKAGE_PIN T11 [get_ports rx_pin_0]
set_property IOSTANDARD LVCMOS33 [get_ports rx_pin_0]
set_false_path -from [get_ports rx_pin_0]

set_property SLEW SLOW [get_ports {I_out Q_out}]
set_property DRIVE 4   [get_ports {I_out Q_out}]