module sata_device_ip (
    input wire clk,                 // Link clock (150 MHz for SATA II 3.0 Gbps)
    input wire rst_n,               // Active-low asynchronous reset
    
    // Physical Transceiver Interface (GTX/GTP blackbox)
    input wire [31:0] rx_data,      // 32-bit rx data from SERDES
    input wire [3:0] rx_charisk,     // 4-bit charisK indicators
    output reg [31:0] tx_data,      // 32-bit tx data to SERDES
    output reg [3:0] tx_charisk,     // 4-bit charisK indicators
    
    input wire oob_reset_det,       // Host COMRESET detected
    output reg oob_init_tx,         // Trigger COMINIT
    output reg oob_wake_tx,         // Trigger COMWAKE
    input wire phy_ready,           // Transceiver locked and symbol aligned
    
    // BRAM Interface (Dual-Port Mailbox Access)
    output reg ram_we,
    output reg [14:0] ram_addr,
    output reg [31:0] ram_wdata,
    input wire [31:0] ram_rdata,
    
    // Asynchronous Notification & Signaling
    input wire peer_write_event,    // Packet written by peer host
    output reg local_write_event    // Packet written by local host
);

    // SATA Primitives (DWORDs, K28.5 is 0x7C, K28.3 is 0x7B)
    localparam PRIM_ALIGN   = 32'h7B7B7C7C;
    localparam PRIM_SYNC    = 32'hB5B57C7C;
    localparam PRIM_SOF     = 32'h35357C7C;
    localparam PRIM_EOF     = 32'h85857C7C;
    localparam PRIM_XRDY    = 32'h4A4A7C7C;
    localparam PRIM_RRDY    = 32'h4B4B7C7C;
    localparam PRIM_WTRM    = 32'h58587C7C;
    localparam PRIM_R_OK    = 32'h5C5C7C7C;
    localparam PRIM_R_ERR   = 32'h5D5D7C7C;

    // Link Layer State Machine
    localparam L_RESET      = 3'd0;
    localparam L_IDLE       = 3'd1;
    localparam L_RX_FIS     = 3'd2;
    localparam L_TX_REQ     = 3'd3;
    localparam L_TX_DATA    = 3'd4;
    localparam L_TX_WTRM    = 3'd5;

    reg [2:0] link_state;
    reg [2:0] next_link_state;

    // Transport Layer State Machine
    localparam T_IDLE           = 4'd0;
    localparam T_PARSE_FIS      = 4'd1;
    localparam T_CMD_IDENTIFY   = 4'd2;
    localparam T_CMD_READ       = 4'd3;
    localparam T_CMD_WRITE      = 4'd4;
    localparam T_SEND_STATUS    = 4'd5;
    localparam T_SEND_AN        = 4'd6;

    reg [3:0] trans_state;
    reg [3:0] next_trans_state;

    // Registers & Internal State
    reg [31:0] fis_rx_buf [0:63];   // Buffer to hold incoming FIS (max 256 bytes)
    reg [7:0]  fis_rx_len;
    reg [31:0] fis_tx_buf [0:63];   // Buffer to hold outgoing FIS
    reg [7:0]  fis_tx_len;
    reg [7:0]  fis_tx_ptr;

    reg [63:0] lba_addr;            // Target LBA parsed from FIS 27h
    reg [15:0] sector_count;        // Sector count parsed from FIS 27h
    reg [7:0]  command;             // ATA command parsed from FIS 27h

    reg        peer_event_pending;

    // OOB Handshake Control
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            oob_init_tx <= 1'b0;
            oob_wake_tx <= 1'b0;
        end else if (oob_reset_det) begin
            oob_init_tx <= 1'b1;
            oob_wake_tx <= 1'b1;
        end else begin
            oob_init_tx <= 1'b0;
            oob_wake_tx <= 1'b0;
        end
    end

    // Track peer write events for Asynchronous Notification
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            peer_event_pending <= 1'b0;
        end else if (peer_write_event) begin
            peer_event_pending <= 1'b1;
        end else if (trans_state == T_SEND_AN) begin
            peer_event_pending <= 1'b0;
        end
    end

    // Link Layer State Register
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            link_state <= L_RESET;
        else
            link_state <= next_link_state;
    end

    // Link Layer Next State & Outputs
    always @(*) begin
        next_link_state = link_state;
        tx_data = PRIM_SYNC;
        tx_charisk = 4'b0001; // Assuming lowest byte has K character (K28.5)

        case (link_state)
            L_RESET: begin
                if (phy_ready)
                    next_link_state = L_IDLE;
            end
            
            L_IDLE: begin
                tx_data = PRIM_SYNC;
                if (rx_data == PRIM_SOF && rx_charisk == 4'b0001) begin
                    next_link_state = L_RX_FIS;
                end else if (trans_state == T_CMD_IDENTIFY || trans_state == T_CMD_READ || 
                             trans_state == T_SEND_STATUS || trans_state == T_SEND_AN) begin
                    next_link_state = L_TX_REQ;
                end
            end

            L_TX_REQ: begin
                tx_data = PRIM_XRDY;
                if (rx_data == PRIM_RRDY && rx_charisk == 4'b0001) begin
                    next_link_state = L_TX_DATA;
                end
            end

            L_TX_DATA: begin
                tx_data = PRIM_SOF;
                next_link_state = L_TX_WTRM; // Simplification of multi-cycle transmission
            end

            L_TX_WTRM: begin
                tx_data = PRIM_WTRM;
                if (rx_data == PRIM_R_OK && rx_charisk == 4'b0001) begin
                    next_link_state = L_IDLE;
                end else if (rx_data == PRIM_R_ERR && rx_charisk == 4'b0001) begin
                    next_link_state = L_IDLE; // Retries would be handled in standard Link layer
                end
            end

            L_RX_FIS: begin
                // Capture incoming FIS until PRIM_EOF is received
                tx_data = PRIM_RRDY;
                if (rx_data == PRIM_EOF && rx_charisk == 4'b0001) begin
                    next_link_state = L_IDLE;
                end
            end

            default: next_link_state = L_RESET;
        endcase
    end

    // Transport/Command Layer State Register
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            trans_state <= T_IDLE;
        else
            trans_state <= next_trans_state;
    end

    // Transport Layer Next State Logic
    always @(*) begin
        next_trans_state = trans_state;
        case (trans_state)
            T_IDLE: begin
                if (link_state == L_RX_FIS)
                    next_trans_state = T_PARSE_FIS;
                else if (peer_event_pending && link_state == L_IDLE)
                    next_trans_state = T_SEND_AN;
            end

            T_PARSE_FIS: begin
                if (fis_rx_buf[0][7:0] == 8'h27) begin // Register Host-to-Device FIS
                    if (command == 8'hEC) // IDENTIFY DEVICE
                        next_trans_state = T_CMD_IDENTIFY;
                    else if (command == 8'h25 || command == 8'h20) // READ DMA EXT / READ SECTORS
                        next_trans_state = T_CMD_READ;
                    else if (command == 8'h35 || command == 8'h30) // WRITE DMA EXT / WRITE SECTORS
                        next_trans_state = T_CMD_WRITE;
                    else
                        next_trans_state = T_SEND_STATUS;
                end else begin
                    next_trans_state = T_IDLE;
                end
            end

            T_CMD_IDENTIFY: begin
                if (link_state == L_IDLE)
                    next_trans_state = T_SEND_STATUS;
            end

            T_CMD_READ: begin
                if (link_state == L_IDLE)
                    next_trans_state = T_SEND_STATUS;
            end

            T_CMD_WRITE: begin
                if (link_state == L_IDLE)
                    next_trans_state = T_SEND_STATUS;
            end

            T_SEND_STATUS: begin
                if (link_state == L_IDLE)
                    next_trans_state = T_IDLE;
            end

            T_SEND_AN: begin
                if (link_state == L_IDLE)
                    next_trans_state = T_IDLE;
            end

            default: next_trans_state = T_IDLE;
        endcase
    end

    // Receive FIS Capture Logic
    reg [5:0] rx_idx;
    always @(posedge clk) begin
        if (link_state == L_IDLE) begin
            rx_idx <= 6'd0;
        end else if (link_state == L_RX_FIS) begin
            if (rx_charisk == 4'b0000) begin
                fis_rx_buf[rx_idx] <= rx_data;
                rx_idx <= rx_idx + 1'b1;
            end
        end
    end

    // Parse command parameters from Register Host-to-Device FIS (FIS Type 27h)
    always @(posedge clk) begin
        if (trans_state == T_PARSE_FIS) begin
            command <= fis_rx_buf[0][23:16];
            lba_addr[7:0]   <= fis_rx_buf[1][31:24];
            lba_addr[15:8]  <= fis_rx_buf[2][7:0];
            lba_addr[23:16] <= fis_rx_buf[2][15:8];
            lba_addr[31:24] <= fis_rx_buf[2][31:24];
            lba_addr[39:32] <= fis_rx_buf[3][7:0];
            lba_addr[47:40] <= fis_rx_buf[3][15:8];
            sector_count[7:0]  <= fis_rx_buf[3][31:24];
            sector_count[15:8] <= fis_rx_buf[4][7:0];
        end
    end

    // RAM Access and Protocol Actions
    reg [9:0] dma_counter;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            ram_we <= 1'b0;
            ram_addr <= 15'd0;
            ram_wdata <= 32'd0;
            local_write_event <= 1'b0;
            dma_counter <= 10'd0;
        end else begin
            ram_we <= 1'b0;
            local_write_event <= 1'b0;

            case (trans_state)
                T_CMD_IDENTIFY: begin
                    // Prepopulate Identify Data response in TX FIS buffer
                    fis_tx_buf[0] <= 32'h0000_0034; // Register Device-to-Host FIS header
                    fis_tx_buf[1] <= 32'h0000_0050; // Status: Ready, Command completion
                    // Standard identify parameters stating NATA virtual device capability
                    fis_tx_buf[2] <= 32'h4E41_5441; // "NATA" signature model
                    fis_tx_buf[3] <= 32'h2020_454D; // "EM"
                    fis_tx_buf[4] <= 32'h554C_4154; // "ULAT"
                    fis_tx_buf[5] <= 32'h4F52_2020; // "OR"
                    fis_tx_len <= 8'd6;
                end

                T_CMD_READ: begin
                    // Read sector from BRAM
                    ram_addr <= lba_addr[14:0] * 128 + dma_counter[6:0];
                    if (dma_counter < 128) begin
                        dma_counter <= dma_counter + 1'b1;
                        fis_tx_buf[dma_counter] <= ram_rdata;
                    end
                end

                T_CMD_WRITE: begin
                    // Write incoming payload to BRAM
                    if (link_state == L_RX_FIS && rx_charisk == 4'b0000) begin
                        ram_we <= 1'b1;
                        ram_addr <= lba_addr[14:0] * 128 + dma_counter[6:0];
                        ram_wdata <= rx_data;
                        dma_counter <= dma_counter + 1'b1;
                        if (dma_counter == 127) begin
                            local_write_event <= 1'b1; // Trigger packet-written notification to peer
                        end
                    end
                end

                T_SEND_STATUS: begin
                    dma_counter <= 10'd0;
                    fis_tx_buf[0] <= 32'h0050_0034; // Status: Ready, Status Register: 0x50
                    fis_tx_buf[1] <= 32'h0000_0000;
                    fis_tx_len <= 8'd2;
                end

                T_SEND_AN: begin
                    // Send Asynchronous Notification to Host (Status Register: 0x50 with notification bit)
                    fis_tx_buf[0] <= 32'h0850_0034; // Set Interrupt/Notification bit (bit 3 of byte 1)
                    fis_tx_buf[1] <= 32'h0000_0000;
                    fis_tx_len <= 8'd2;
                end

                default: begin
                    dma_counter <= 10'd0;
                end
            endcase
        end
    end

endmodule
