// Simulation-only stubs for Xilinx primitives referenced by nata_top.
// Not for synthesis. Compile this file with iverilog alongside the RTL.

`timescale 1ns/1ps

// Differential input buffer: pass through the positive leg for sim.
module IBUFDS (
    input  wire I,
    input  wire IB,
    output wire O
);
    assign O = I;
endmodule
