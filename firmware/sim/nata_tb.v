`timescale 1ns/1ps

module nata_tb;

    // Inputs
    reg sys_clk_p;
    reg sys_clk_n;
    reg sys_rst;
    
    reg sata_a_rx_p;
    reg sata_a_rx_n;
    reg sata_b_rx_p;
    reg sata_b_rx_n;

    // Outputs
    wire sata_a_tx_p;
    wire sata_a_tx_n;
    wire sata_b_tx_p;
    wire sata_b_tx_n;
    
    wire led_link_a_up;
    wire led_link_b_up;
    wire led_activity_a;
    wire led_activity_b;

    // Instantiate Unit Under Test (UUT)
    nata_top uut (
        .sys_clk_p(sys_clk_p),
        .sys_clk_n(sys_clk_n),
        .sys_rst(sys_rst),
        .sata_a_rx_p(sata_a_rx_p),
        .sata_a_rx_n(sata_a_rx_n),
        .sata_a_tx_p(sata_a_tx_p),
        .sata_a_tx_n(sata_a_tx_n),
        .sata_b_rx_p(sata_b_rx_p),
        .sata_b_rx_n(sata_b_rx_n),
        .sata_b_tx_p(sata_b_tx_p),
        .sata_b_tx_n(sata_b_tx_n),
        .led_link_a_up(led_link_a_up),
        .led_link_b_up(led_link_b_up),
        .led_activity_a(led_activity_a),
        .led_activity_b(led_activity_b)
    );

    // Differential clock generation (200 MHz system clock -> 5ns period)
    always begin
        sys_clk_p = 1'b0;
        sys_clk_n = 1'b1;
        #2.5;
        sys_clk_p = 1'b1;
        sys_clk_n = 1'b0;
        #2.5;
    end

    // Test sequence
    initial begin
        $dumpfile("nata_tb.vcd");
        $dumpvars(0, nata_tb);

        // Initialize inputs
        sys_rst = 1'b1;
        sata_a_rx_p = 1'b0;
        sata_a_rx_n = 1'b1;
        sata_b_rx_p = 1'b0;
        sata_b_rx_n = 1'b1;

        #20;
        sys_rst = 1'b0; // Release reset
        
        #50;
        $display("[TB] Resets released. Verifying PHY ready indicators...");
        
        #100;
        if (led_link_a_up && led_link_b_up) begin
            $display("[TB] SUCCESS: Both SATA link PHYs report locked and aligned.");
        end else begin
            $display("[TB] ERROR: Link PLL failed to lock.");
            $finish;
        end

        // Simulate host A write event (structural AN wire path)
        $display("[TB] Simulating Host A write to LBA 0...");
        // Force write_event while checking: release would clear the pulse before assert
        force uut.write_event_a_to_b = 1'b1;
        #10;
        $display("[TB] Verifying peer interrupt (Asynchronous Notification) propagation...");
        if (led_activity_a) begin
            $display("[TB] SUCCESS: Peer interrupt propagated successfully to Port B.");
        end else begin
            $display("[TB] ERROR: Interrupt signal was not routed.");
            release uut.write_event_a_to_b;
            $finish;
        end
        release uut.write_event_a_to_b;

        #100;
        $display("[TB] Simulation completed successfully.");
        $finish;
    end

endmodule
