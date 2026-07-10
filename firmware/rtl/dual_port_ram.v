module dual_port_ram (
    input wire clk_a,
    input wire we_a,
    input wire [14:0] addr_a,
    input wire [31:0] din_a,
    output reg [31:0] dout_a,

    input wire clk_b,
    input wire we_b,
    input wire [14:0] addr_b,
    input wire [31:0] din_b,
    output reg [31:0] dout_b
);

    // 32,768 x 32-bit memory array (128 KB total)
    // Fits LBA 0 to 255 (each 512 bytes = 128 DWORDs)
    reg [31:0] ram [0:32767];

    always @(posedge clk_a) begin
        if (we_a) begin
            ram[addr_a] <= din_a;
        end
        dout_a <= ram[addr_a];
    end

    always @(posedge clk_b) begin
        if (we_b) begin
            ram[addr_b] <= din_b;
        end
        dout_b <= ram[addr_b];
    end

endmodule
