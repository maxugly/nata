module nata_top (
    input wire sys_clk_p,           // 200 MHz differential system clock input (+)
    input wire sys_clk_n,           // 200 MHz differential system clock input (-)
    input wire sys_rst,             // Hardware reset button (active-high)
    
    // Port A SATA Differential Signals (Directly routed to FPGA transceivers)
    input wire sata_a_rx_p,
    input wire sata_a_rx_n,
    output wire sata_a_tx_p,
    output wire sata_a_tx_n,
    
    // Port B SATA Differential Signals
    input wire sata_b_rx_p,
    input wire sata_b_rx_n,
    output wire sata_b_tx_p,
    output wire sata_b_tx_n,
    
    // Board status LEDs
    output wire led_link_a_up,
    output wire led_link_b_up,
    output wire led_activity_a,
    output wire led_activity_b
);

    // Global signals
    wire clk_150mhz;
    wire sys_rst_n = ~sys_rst;

    // -------------------------------------------------------------
    // Clocking & PLL (Differential clock buffer)
    // -------------------------------------------------------------
    wire sys_clk_ibuf;
    IBUFDS ibufds_sys_clk (
        .I(sys_clk_p),
        .IB(sys_clk_n),
        .O(sys_clk_ibuf)
    );

    // Mock PLL for simulation / design verification
    // In hardware, this is managed by Xilinx Mixed-Mode Clock Manager (MMCM)
    assign clk_150mhz = sys_clk_ibuf; 

    // -------------------------------------------------------------
    // Port A Transceiver / GTP Blackbox
    // -------------------------------------------------------------
    wire [31:0] rx_data_a;
    wire [3:0]  rx_charisk_a;
    wire [31:0] tx_data_a;
    wire [3:0]  tx_charisk_a;
    wire        phy_ready_a;
    wire        oob_reset_det_a;
    wire        oob_init_tx_a;
    wire        oob_wake_tx_a;

    // GTP Primitive representation
    // GTP_DUAL #(...) gtp_port_a (...)
    assign phy_ready_a = 1'b1; // Simplified for simulation verification
    assign oob_reset_det_a = 1'b0;

    // -------------------------------------------------------------
    // Port B Transceiver / GTP Blackbox
    // -------------------------------------------------------------
    wire [31:0] rx_data_b;
    wire [3:0]  rx_charisk_b;
    wire [31:0] tx_data_b;
    wire [3:0]  tx_charisk_b;
    wire        phy_ready_b;
    wire        oob_reset_det_b;
    wire        oob_init_tx_b;
    wire        oob_wake_tx_b;

    assign phy_ready_b = 1'b1;
    assign oob_reset_det_b = 1'b0;

    // -------------------------------------------------------------
    // BRAM Interface Signals
    // -------------------------------------------------------------
    wire        ram_we_a;
    wire [14:0] ram_addr_a;
    wire [31:0] ram_wdata_a;
    wire [31:0] ram_rdata_a;

    wire        ram_we_b;
    wire [14:0] ram_addr_b;
    wire [31:0] ram_wdata_b;
    wire [31:0] ram_rdata_b;

    // Mailbox Write Intercept / Interrupt Events
    wire        write_event_a_to_b;
    wire        write_event_b_to_a;

    // -------------------------------------------------------------
    // Module Instances
    // -------------------------------------------------------------

    // Dual-Port Mailbox BRAM
    dual_port_ram mailbox_ram (
        .clk_a(clk_150mhz),
        .we_a(ram_we_a),
        .addr_a(ram_addr_a),
        .din_a(ram_wdata_a),
        .dout_a(ram_rdata_a),

        .clk_b(clk_150mhz),
        .we_b(ram_we_b),
        .addr_b(ram_addr_b),
        .din_b(ram_wdata_b),
        .dout_b(ram_rdata_b)
    );

    // SATA Device Controller Port A
    sata_device_ip sata_port_a (
        .clk(clk_150mhz),
        .rst_n(sys_rst_n),
        .rx_data(rx_data_a),
        .rx_charisk(rx_charisk_a),
        .tx_data(tx_data_a),
        .tx_charisk(tx_charisk_a),
        .oob_reset_det(oob_reset_det_a),
        .oob_init_tx(oob_init_tx_a),
        .oob_wake_tx(oob_wake_tx_a),
        .phy_ready(phy_ready_a),
        .ram_we(ram_we_a),
        .ram_addr(ram_addr_a),
        .ram_wdata(ram_wdata_a),
        .ram_rdata(ram_rdata_a),
        .peer_write_event(write_event_b_to_a),
        .local_write_event(write_event_a_to_b)
    );

    // SATA Device Controller Port B
    sata_device_ip sata_port_b (
        .clk(clk_150mhz),
        .rst_n(sys_rst_n),
        .rx_data(rx_data_b),
        .rx_charisk(rx_charisk_b),
        .tx_data(tx_data_b),
        .tx_charisk(tx_charisk_b),
        .oob_reset_det(oob_reset_det_b),
        .oob_init_tx(oob_init_tx_b),
        .oob_wake_tx(oob_wake_tx_b),
        .phy_ready(phy_ready_b),
        .ram_we(ram_we_b),
        .ram_addr(ram_addr_b),
        .ram_wdata(ram_wdata_b),
        .ram_rdata(ram_rdata_b),
        .peer_write_event(write_event_a_to_b),
        .local_write_event(write_event_b_to_a)
    );

    // -------------------------------------------------------------
    // Status & Diagnostics Wiring
    // -------------------------------------------------------------
    assign led_link_a_up   = phy_ready_a;
    assign led_link_b_up   = phy_ready_b;
    assign led_activity_a  = write_event_a_to_b;
    assign led_activity_b  = write_event_b_to_a;

endmodule
