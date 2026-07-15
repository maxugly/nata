Here is the formal, deep-dive engineering whitepaper for the NATA protocol. This outlines exactly why the physics support the madness, where the protocol breaks, and the precise architecture required to bridge the gap using Field-Programmable Gate Arrays and custom kernel space routing.

# **Architecture Specification: NATA (Not Advanced Technology Attachment)**

**Draft: 1.0**  
**Topic:** Layer 1/Layer 2 Encapsulation of IP Traffic over SATA 3.0 Storage Bus

## **1\. The PHY Layer: Electrical Plausibility**

From a pure Layer 1 (Physical) perspective, bridging two motherboards with a modified SATA cable is completely electrically sound.  
SATA uses **AC-Coupled Differential Signaling**. There is no continuous DC voltage flowing between the motherboard and the drive. Instead, microscopic in-line capacitors block DC current, allowing only the high-frequency AC data waves to pass. Because of this, crossing the wires carries zero risk of short-circuiting a motherboard or dumping destructive voltage into a controller.  
If you slice a SATA cable and wire Host A's $Tx+/Tx-$ pair directly to Host B's $Rx+/Rx-$ pair, you have successfully built a crossover.  
When the motherboards power on, they initiate **Out-Of-Band (OOB) Signaling**.

1. Host A pulses a COMRESET analog wave.  
2. Host B's PHY hardware detects the pulse and inherently replies with a COMINIT.  
3. Both sides exchange COMWAKE signals.

At this exact moment, the Phase-Locked Loops (PLLs) on both motherboards will successfully synchronize. The electrical PHYs will negotiate the speed and successfully lock in at a blistering $3.0\\text{ GHz}$. The link is physically alive.

## **2\. The Link Layer: The Protocol Stalemate**

The physics work, but the logic fails milliseconds later during the **Link Initialization Phase**.  
SATA communicates using packets called **FIS (Frame Information Structures)**. The SATA protocol dictates a rigid Master/Slave hierarchy hard-coded into the AHCI silicon.

* A Host can only transmit a FIS Type 27h (Register Host-to-Device).  
* A Host can only receive a FIS Type 34h (Register Device-to-Host).

When our physical link goes live, Host A immediately transmits a 27h packet, declaring its presence. Host B simultaneously transmits a 27h packet.  
Both hardware controllers are hit with incoming Host packets. The silicon state-machines have no logic gates to process an incoming 27h packet. Both controllers instantly classify it as a fatal link-layer error, drop the connection, and enter an infinite retry loop.  
Because this behavior is baked into the physical Application-Specific Integrated Circuit (ASIC) of the motherboard, it cannot be patched in software. We must intercept the signals with a hardware middleman.

## **3\. The FPGA Middleman: Dual-Target Emulation**

To resolve the FIS collision, we introduce a dedicated hardware bridge utilizing an **FPGA (Field-Programmable Gate Array)** equipped with high-speed Multi-Gigabit Transceivers (like a Xilinx Artix-7 or Kintex-7).  
The FPGA acts as a digital mirror. It does not run an operating system; it runs synthesized hardware logic (Verilog/VHDL) that perfectly mimics the behavior of a mechanical hard drive's logic board.

### **Architecture**

1. **The Dual PHY Cores:** The FPGA is programmed with two distinct SATA Device IP cores. Port 1 connects to PC A. Port 2 connects to PC B.  
2. **The Handshake:** When PC A sends its 27h Host FIS, the FPGA instantly swallows it and replies with a perfectly formatted 34h Device FIS. PC A's AHCI controller breathes a sigh of relief, reports a 500GB SSD has been attached, and alerts the Linux kernel.  
3. **The Dual-Port BRAM Mailbox:** Inside the FPGA, we allocate a block of ultra-fast static memory (Block RAM). This BRAM is dual-ported, meaning Port 1 and Port 2 can read and write to it simultaneously without locking each other out.  
4. **LBA Mapping:** We program the FPGA to present this BRAM to the motherboards as raw Logical Block Addressing (LBA) sectors.  
   * LBA Sectors 0 \- 100 are mapped as the *Transmit Queue* for PC A.  
   * LBA Sectors 101 \- 200 are mapped as the *Transmit Queue* for PC B.

The hardware illusion is now complete. The burden of turning this fake hard drive into a network switch falls entirely to the operating system.

## **4\. The Software Stack: NATA Kernel Module**

We are not going to use heavy user-space daemons. Operating in a lightweight, bare-metal Linux environment like Void Linux allows us to drop a custom kernel module directly into the stack and manage it strictly via runit and native iproute2 commands.  
The custom module, nata.ko, is a hybrid beast. It must simultaneously interface with the Linux Block Layer (libata) and the Linux Network Stack (net\_device).

### **Packet Encapsulation (IP over SCSI)**

When you attempt to ping PC B, the Linux network stack generates a standard Ethernet frame containing an IPv4 packet. It hands this packet to our nata0 virtual interface.  
The NATA driver catches the packet and performs the encapsulation:

1. It pads the variable-length Ethernet frame to fit perfectly into a 512-byte SCSI block.  
2. It generates a standard SCSI WRITE(10) command.  
3. It bypasses the standard file system entirely and blasts that raw block directly to LBA Sector 0 on the FPGA.

### **Asynchronous Interrupt Handling**

The hardest part of storage-based networking is knowing when a packet arrives. Standard hard drives don't "push" data to the CPU; the CPU asks for it. If PC B runs a continuous while loop constantly reading Sector 0 to see if a packet arrived, it will pin a CPU core at 100% utilization.  
To fix this, NATA exploits a SATA feature called **Asynchronous Notification (AN)**—originally designed to let CD-ROM drives tell the OS when a user inserted a disk.

* When the FPGA detects a write from PC A to the mailbox, it fires a hardware interrupt (AN) down the wire to PC B.  
* The AHCI controller on PC B catches the interrupt and wakes up our kernel module.  
* The kernel module instantly fires a SCSI READ(10) command, pulls the 512-byte block from the FPGA, strips off the SCSI header, and hands the raw IPv4 packet up to the local network stack.

### **Deployment**

To bring the interface online cleanly without systemd interference, we load the module, verify the interface exists, and assign our routing coordinates directly.

Bash  
sudo insmod nata.ko

Bash  
sudo ip link set nata0 up

Bash  
sudo ip addr add 10.0.0.1/24 dev nata0

Once the routes are set, the network traffic seamlessly drops beneath the abstraction layer, plunging through the SCSI subsystem, out the AHCI controller, and across the $3\\text{ GHz}$ Twinax differential pairs.  
It is a masterpiece of protocol abuse.