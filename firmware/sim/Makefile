# Structural smoke test for nata_top (Icarus Verilog)
IVERILOG ?= iverilog
VVP      ?= vvp
OUT      ?= nata_tb.vvp

SRCS = \
	nata_tb.v \
	xilinx_sim_stubs.v \
	../rtl/nata_top.v \
	../rtl/sata_device_ip.v \
	../rtl/dual_port_ram.v

.PHONY: all sim clean

all: sim

sim: $(OUT)
	$(VVP) $(OUT)

$(OUT): $(SRCS)
	$(IVERILOG) -o $(OUT) -g2012 $(SRCS)

clean:
	rm -f $(OUT) nata_tb.vcd
