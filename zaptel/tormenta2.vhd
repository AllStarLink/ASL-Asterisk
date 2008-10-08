-- Tormenta2 -- PCI Telephony Interface Card -- VHDL for Xilinx Part
-- version 1.4, 10/10/2002.
-- Copyright (c) 2001-2002, Jim Dixon. 
--
-- Jim Dixon <jim@lambdatel.com>
-- Mark Spencer <mark@linux-support.net>
--
-- This program is free software, and the design, schematics, layout,
-- and artwork for the hardware on which it runs is free, and all are
-- distributed under the terms of the GNU General Public License.
--
-- Thanks to Mark and the gang at Linux Support Services for the contribution
-- of the initial buffering code.
--
--

-- The A4 die of the Dallas 21Q352 chip has a bug in it (well, it has several actually,
-- but this is the one that effects us the most) where when you have it in IBO mode
-- (where all 4 framers are combined into 1 8.192 Mhz backplane), the receive data
-- comes out of the chip late. So late, in fact, that its an entire HALF clock cycle 
-- off. So what we had to do is have a separate RSYSCLK signal (which was the TSYSCLK
-- signal inverted) and a separate RSYNC signal (which corresponds to the RSYSCLK inverted
-- signal as opposed to the TSYSCLK) that was 1/2 clock cycle early, so that the data comes
-- out at the correct time. 

library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.STD_LOGIC_ARITH.ALL;
use IEEE.STD_LOGIC_UNSIGNED.ALL;

entity tormenta2 is
    Port ( 
-- GCK0 signal (Local bus clock)
			  CLK : in std_logic;
-- GCK1 signal 8.192 Mhz clock from mk1574 and drives SYSCLK's on Dallas chip
		     CLK8192 : in std_logic;
-- Tx Framing Pulse to Dallas chip
           TSSYNC : out std_logic;
-- Rx Framing Pulse to Dallas chip
           RSYNC : out std_logic;
-- 8 Khz square wave for input to mk1574 (RCLKO divided by 193)
           KHZ8000 : out std_logic;
-- RSER from Dallas chip (received serial data)
           RSER : in std_logic;
-- TSER to Dallas chip (transmitted serial data)
           TSER : out std_logic;
-- RCLK output to Dallas chip (TCLK)  (1.544 Mhz)
           RCLKO : out std_logic;
-- RCLK 1-4 are RCLK inputs from SCT's, 0 is 1.544 Mhz oscillator
           RCLK : in std_logic_vector(4 downto 0);
--           LCLK is tied to GCK0, so you dont specify it here.
--			    CCLK, CPROGRAM, and CDONE are tied to dedicated pins, so you dont either.
-- Local bus Data Bus
           D : inout std_logic_vector(31 downto 0);
-- Local bus Address Bus
           ADDR : in std_logic_vector(11 downto 2);
-- Local bus Byte Enable lines (also BE0 is A0 and BE1 is A1 for 8 bit operations)
           BE : in std_logic_vector(3 downto 0);
-- Local bus "WR" signal
           WR : in std_logic;
-- Local bus "RD" signal
           RD : in std_logic;
-- Local bus READY signal out (also Configuration BUSY signal)
           READY : out std_logic;
-- Local bus INTerrupt signal
           INT : out std_logic;
-- Chip selects for Dallas chip SCT's 1 thru 4
           CS : out std_logic_vector(4 downto 1);
-- Dallas chip WRite signal
           DWR : out std_logic;
-- Dallas chip ReaD signal
           DRD : out std_logic;
-- Dallas chip INTerrupt signal in
           DINT : in std_logic;
-- LED's output
			  LEDS : out std_logic_vector(7 downto 0);
-- Board ID input
			  BOARDID : in std_logic_vector(3 downto 0);
-- TEST pins
			  TEST1 : inout std_logic;
			  TEST2 : inout std_logic;
			  TEST3 : inout std_logic;
			  TEST4 : inout std_logic;
-- BTERM output
			  BTERM : out std_logic;
-- MASTER output
			  MASTER : out std_logic;
-- XSYNCIN input
			  XSYNCIN: in std_logic;
-- XSYNCOUT output
			  XSYNCOUT: out std_logic);
end tormenta2;

architecture behavioral of tormenta2 is

component RAMB4_S1_S16 
  port (
    ADDRA: IN std_logic_vector(11 downto 0);
	 ADDRB: IN std_logic_vector(7 downto 0);
	 DIA:	  IN std_logic_vector(0 downto 0);
	 DIB:	  IN std_logic_vector(15 downto 0);
	 WEA:   IN std_logic;
	 WEB:   IN std_logic;
	 CLKA:  IN std_logic;
	 CLKB:  IN std_logic;
	 RSTA:  IN std_logic;
	 RSTB:  IN std_logic;
	 ENA:   IN std_logic;
	 ENB:   IN std_logic;
	 DOA:   OUT std_logic_vector(0 downto 0);
	 DOB:   OUT std_logic_vector(15 downto 0));
END component;

-- Counter for wait state/Dallas generator
signal waitcnt : std_logic_vector(2 downto 0);
-- Global counter
signal counter:	std_logic_vector(13 downto 0);
-- Local copy of Global counter
signal lcounter:	std_logic_vector(13 downto 0);
-- Position in a given buffer
signal position:  std_logic_vector(11 downto 0);
-- Latched buffer position
signal lposition:  std_logic_vector(11 downto 0);
-- dbuf represents the buffer that is currently being
-- operated upon by the T1 part, while not dbuf represents
-- the buffer that the bus side is operating with
signal dbuf:	std_logic;
-- Lathed dbuf signal
signal ldbuf:	std_logic;
-- Which ram of the buffer we are currently operating with
-- (0 = top, 1 = bottom)
signal ramno: 	std_logic;
-- Latched ramno signal
signal lramno: 	std_logic;
-- Serial output from first upper 16-bit memory
signal txqt1out: std_logic;
-- Serial output from second upper 16-bit memory
signal txqt2out: std_logic;
-- Serial output from first lower 16-bit memory
signal txqb1out: std_logic;
-- Serial output from second lower 16-bit memory
signal txqb2out: std_logic;
-- Parallel output from first 32-bits of memory
signal rxq1out: std_logic_vector(31 downto 0);
-- Parallel output from second 32-bits of memory
signal rxq2out: std_logic_vector(31 downto 0);
-- Ground bus for unnecessary inputs
signal gndbus: std_logic_vector(15 downto 0);
-- RWR: Write enable for ram
signal RWR: std_logic;
-- RRD: Read enable for ram
signal RRD: std_logic;
-- Local version of 1.544 Mhz clock to be output
signal lclk: std_logic;
-- 8khz counter
signal cnt193: std_logic_vector(7 downto 0);
-- Which of the received clocks to propagate
signal clkreg: std_logic_vector(2 downto 0);
-- First Control register
signal ctlreg: std_logic_vector(7 downto 0);
-- Second Control register
signal ctlreg1: std_logic_vector(7 downto 0);
-- Status register
signal statreg: std_logic_vector(2 downto 0);
-- LED register
signal ledreg: std_logic_vector(7 downto 0);
-- LED cycle counter
signal ledcnt: std_logic_vector(1 downto 0);
-- Signal actually driving Rx buffers (after Rxserial loopback mux)
signal xrser: std_logic;
-- Signal actually driven by Tx buffers (before Txserial loopback mux)
signal xtser: std_logic;
signal tssync_local: std_logic;
signal rsync_reva: std_logic;

-- Register definitions:

-- Write:
-- 0xC00 -- clkreg (sync source) 0=free run, 1=span 1, 2=span 2, 3=span 3, 4=span 4, 5=external.
-- 0xC01 -- ctlreg as follows:
--	bit 0 - Interrupt Enable
--   bit 1 - Drives "TEST1" signal ("Interrupt" outbit)
--	bit 2 - Dallas Interrupt Enable (Allows DINT signal to drive INT)
--	bit 3 - Enable External Synronization Drive (MASTER signal).
--	bit 4 - Select E1 Divisor Mode (0 for T1, 1 for E1)
--   bit 5 - Remote serial loopback (When set to 1, TSER is driven from RSER)
--	bit 6 - Local serial loopback (When set to 1, Rx buffers are driven from Tx buffers)
--   bit 7 - Interrupt Acknowledge (set to 1 to acknowledge interrupt)
-- 0xC02 -- LED register as follows:
--	bit 0 - Span 1 Green
-- 	bit 1 - Span 1 Red
--	bit 2 - Span 2 Green
--	bit 3 - Span 2 Red
--	bit 4 - Span 3 Green
--	bit 5 - Span 3 Red
-- 	bit 6 - Span 4 Green
-- 	bit 7 - Span 4 Red
--	NOTE: turning on both red and green yields yellow.
-- 0xC03 -- TEST2, writing to bit 0 drives TEST2 pin.
-- 0xC04 -- ctlreg1 as follows:
--	bit 0 - Non-REV.A Timing mode (set for REV. B Dallas chip and higher)
--
-- Read:
-- 0xC00 -- statreg as follows:
--	bit 0 - Interrupt Enabled
--	bit 1 - Interrupt Active
-- 	bit 2 - Dallas Chip Interrupt Active
-- 0xC01 -- boardid as follows:
--   bits 0-3 Board ID bits 0-3 (from rotary dip switch)
	
begin

	-- Create statreg for user to be able to read
	statreg(0) <= ctlreg(0);  -- Interrupt enable status
	statreg(2) <= not DINT;   -- Dallas chip interrupt request
	-- Tie INT signal to bit in statreg
	INT <= statreg(1) or ((not DINT) and ctlreg(2));

	MASTER <= ctlreg(3); -- Control Bit to enable External Sync Driver

	TEST1 <= ctlreg(1); -- Reflect "Interrupt" Outbit
	TEST3 <= statreg(1); -- Reflect Interrupt Status
	TEST4 <= RSER;

	BTERM <= '1';	-- Leave this not enabled for now.

	-- Which ram we read into is from the 5th LSB of the counter
	ramno <= lcounter(4);
	-- Which buffer we're using is the most significant 
	dbuf <= lcounter(13);
	-- Our position is the bottom 4 bits, inverted, and then
	-- the skip one, and then the next 8 bits.
	position(3 downto 0) <= not lcounter(3 downto 0);
	position(11 downto 4) <= lcounter(12 downto 5);

	gndbus <= "0000000000000000";

	txqt1: RAMB4_S1_S16 port map (
		ADDRA => position,				-- Where are we in transmission 
		ADDRB => ADDR(9 downto 2),	-- Address into our 16-bit words
		DIA(0) => '0',					-- We never write from the serial side
		DIB => D(31 downto 16),		-- Top 16-bits of data bus
		WEA => '0',						-- Never write from serial side
		WEB => not WR,					-- Write when requested
		CLKA => CLK8192,				-- Clock output at 8.192 Mhz
		CLKB => RWR,					-- Clock input when asked to by PCI bus
		ENA => '1',						-- Always enable output
		ENB => dbuf,					-- Enable when dbuf is set.
		DOA(0) => txqt1out,			-- Serial output to be MUXed
		RSTA => '0',					-- No need for silly reset
		RSTB => '0'
		);

	txqt2: RAMB4_S1_S16 port map (
		ADDRA => position,				-- Where are we in transmission 
		ADDRB => ADDR(9 downto 2),	-- Address into our 16-bit words
		DIA(0) => '0',					-- We never write from the serial side
		DIB => D(31 downto 16),		-- Top 16-bits of data bus
		WEA => '0',						-- Never write from serial side
		WEB => not WR,					-- Write when requested
		CLKA => CLK8192,				-- Clock output at 8.192 Mhz
		CLKB => RWR,					-- Clock input when asked to by PCI bus
		ENA => '1',						-- Always enable output
		ENB => not dbuf,				-- Take input from user when not in use.
		DOA(0) => txqt2out,			-- Serial output to be MUXed
		RSTA => '0',					-- No need for silly reset
		RSTB => '0'
		);

	txqb1: RAMB4_S1_S16 port map (
		ADDRA => position,				-- Where are we in transmission 
		ADDRB => ADDR(9 downto 2),	-- Address into our 16-bit words
		DIA(0) => '0',					-- We never write from the serial side
		DIB => D(15 downto 0),		-- Top 16-bits of data bus
		WEA => '0',						-- Never write from serial side
		WEB => not WR,					-- Write when requested
		CLKA => CLK8192,				-- Clock output at 8.192 Mhz
		CLKB => RWR,					-- Clock input when asked to by PCI bus
		ENA => '1',						-- Always enable output
		ENB => dbuf,					-- Enable input when not in use
		DOA(0) => txqb1out,			-- Serial output to be MUXed
		RSTA => '0',					-- No need for silly reset
		RSTB => '0'
		);

	txqb2: RAMB4_S1_S16 port map (
		ADDRA => position,			-- Where are we in transmission 
		ADDRB => ADDR(9 downto 2),	-- Address into our 16-bit words
		DIA(0) => '0',					-- We never write from the serial side
		DIB => D(15 downto 0),		-- Top 16-bits of data bus
		WEA => '0',						-- Never write from serial side
		WEB => not WR,						-- Write when requested
		CLKA => CLK8192,				-- Clock output at 8.192 Mhz
		CLKB => RWR,					-- Clock input when asked to by PCI bus
		ENA => '1',						-- Always enable output
		ENB => not dbuf,				-- Enable when dbuf is set.
		DOA(0) => txqb2out,				-- Serial output to be MUXed
		RSTA => '0',					-- No need for silly reset
		RSTB => '0'
		);

	rxqt1: RAMB4_S1_S16 port map (
		ADDRA => lposition,			-- Where to put the next sample
		ADDRB => ADDR(9 downto 2),	-- Addressable output
		DIA(0) => XRSER,					-- Input from serial from T1
		DIB => gndbus,						-- Never input from bus
		WEA => not lramno,   -- Enable writing when we're in the top
		WEB => '0',
		CLKA => not CLK8192,				-- Clock input from T1
		CLKB => RRD,				-- Clock output from bus
		ENA => not ldbuf,				-- Enable when we're the selected buffer
		ENB => '1',						-- Always enable output (it gets MUXed)
		DOB => rxq1out(31 downto 16), -- Data output to MUX
		RSTA => '0',
		RSTB => '0'
		);
	
	rxqt2: RAMB4_S1_S16 port map (
		ADDRA => lposition,			-- Where to put the next sample
		ADDRB => ADDR(9 downto 2),	-- Addressable output
		DIA(0) => XRSER,				-- Input from serial from T1
		DIB => gndbus,					-- Never input from bus
		WEA => not lramno,				-- Enable writing when we're in the top
		WEB => '0',
		CLKA => not CLK8192,				-- Clock input from T1
		CLKB => RRD,				-- Clock output from bus
		ENA => ldbuf,					-- Enable when we're the selected buffer
		ENB => '1',						-- Always enable output (it gets MUXed)
		DOB => rxq2out(31 downto 16), -- Data output to MUX
		RSTA => '0',
		RSTB => '0'
		);

	rxqb1: RAMB4_S1_S16 port map (
		ADDRA => lposition,			-- Where to put the next sample
		ADDRB => ADDR(9 downto 2),	-- Addressable output
		DIA(0) => XRSER,				-- Input from serial from T1
		DIB => gndbus,					-- Never input from bus
		WEA => lramno,					-- Enable writing when we're in the top
		WEB => '0',
		CLKA => not CLK8192,				-- Clock input from T1
		CLKB => RRD,				-- Clock output from bus
		ENA => not ldbuf,				-- Enable when we're the selected buffer
		ENB => '1',						-- Always enable output (it gets MUXed)
		DOB => rxq1out(15 downto 0), -- Data output to MUX
		RSTA => '0',
		RSTB => '0'
		);

	rxqb2: RAMB4_S1_S16 port map (
		ADDRA => lposition,			-- Where to put the next sample
		ADDRB => ADDR(9 downto 2),	-- Addressable output
		DIA(0) => XRSER,				-- Input from serial from T1
		DIB => gndbus,					-- Never input from bus
		WEA => lramno,					-- Enable writing when we're in the top
		WEB => '0',
		CLKA => not CLK8192,				-- Clock input from T1
		CLKB => RRD,				-- Clock output from bus
		ENA => ldbuf,					-- Enable when we're the selected buffer
		ENB => '1',						-- Always enable output (it gets MUXed)
		DOB => rxq2out(15 downto 0), -- Data output to MUX
		RSTA => '0',
		RSTB => '0'
		);


clkdiv193: process(lclk,ctlreg(4))  -- Divider from 1.544 Mhz (or 2.048 MHZ for E1) to 8 Khz to drive MK1574 via KHZ8000 pin
begin
	if (lclk'event and lclk = '1') then
		cnt193 <= cnt193 + 1;
		if (ctlreg(4) = '0') then -- For T1 operation
			-- Go high after 96 samples and 
			-- low after 193 samples
			if (cnt193 = "01100000") then
				KHZ8000 <= '1';
			elsif (cnt193 = "11000000") then  -- *YES* C0 hex *IS* the correct value. I even checked it on a freq. counter!
				KHZ8000 <= '0';
				cnt193 <= "00000000";
			end if;
		else -- For E1 operation, it naturally divides by 256 (being an 8 bit counter)
			KHZ8000 <= cnt193(7);
		end if;
	end if;
end process clkdiv193;


-- Serial transmit data (TSER) output mux (from RAM outputs)
txmux: process (txqt1out, txqt2out, txqb1out, txqb2out,dbuf,ramno,rser)
begin
	if (dbuf = '0') then
		if (ramno = '0') then
					XTSER <= txqt1out;
		else
					XTSER <= txqb1out;
		end if;
	else
		if (ramno = '0') then
					XTSER <= txqt2out;
		else
					XTSER <= txqb2out;
	   end if;
	end if;
	if (ctlreg(5)='1') then -- If in remote serial loopback
		TSER <= RSER;
	else
		TSER <= XTSER;
	end if;
end process txmux;

-- Stuff to do on rising edge of TSYSCLK
process(CLK8192,lcounter(12 downto 0),ctlreg(7))
begin
	-- Make sure we're on the rising edge
	if (CLK8192'event and CLK8192 = '1') then 
		counter <= counter + 1;
		-- We latch copies of ramno, dbuf, and position on this clock so that they
		-- will be stable when the RX buffer stuff needs them on the other edge of the clock
		lramno <= ramno;
		ldbuf <= dbuf;
		lposition <= position;
		if (lcounter(9 downto 0)="0000000000") then -- Generate TSSYNC signal
			TSSYNC_LOCAL <= '1'; 
		else
			TSSYNC_LOCAL <= '0';
		end if;
		-- If we are on an 8 sample boundary, and interrupts are enabled, 
		if (((lcounter(12 downto 0)="0000000000000") and (ctlreg(0)='1'))) then
			statreg(1) <= '1';
		elsif (ctlreg(7)='1' or ctlreg(0)='0') then
			statreg(1) <= '0';  -- If interrupt ack-ed
		end if;
		-- If we are on an 16 sample boundary, twiddle LED's
		if (lcounter="00000000000000") then
			-- We make this 3 count sequence, because we need 2/3 green and 1/3 red to make
			-- yellow. Half and half makes sorta orange (yuch!). Bit 1 of the ledcnt will
			-- be 0 for 2 counts, then 1 for 1 count. Perfict for making yellow!
			if (ledcnt="10") then
				ledcnt <= "00"; -- 3 count sequence 
			else
				ledcnt <= ledcnt + 1;
			end if;
			-- Logic for LED 1
			if (ledreg(1 downto 0)="11") then -- If yellow, use count seq.
				LEDS(0) <= ledcnt(1);
				LEDS(1) <= not ledcnt(1);
			else -- Otherwise is static
				LEDS(1 downto 0) <= not ledreg(1 downto 0);
			end if;
			-- Logic for LED 2
			if (ledreg(3 downto 2)="11") then -- If yellow, use count seq.
				LEDS(2) <= ledcnt(1);
				LEDS(3) <= not ledcnt(1);
			else -- Otherwise is static
				LEDS(3 downto 2) <= not ledreg(3 downto 2);
			end if;
			-- Logic for LED 3
			if (ledreg(5 downto 4)="11") then -- If yellow, use count seq.
				LEDS(4) <= ledcnt(1);
				LEDS(5) <= not ledcnt(1);
			else -- Otherwise is static
				LEDS(5 downto 4) <= not ledreg(5 downto 4);
			end if;
			-- Logic for LED 4
			if (ledreg(7 downto 6)="11") then -- If yellow, use count seq.
				LEDS(6) <= ledcnt(1);
				LEDS(7) <= not ledcnt(1);
			else -- Otherwise is static
				LEDS(7 downto 6) <= not ledreg(7 downto 6);
			end if;
		end if;
	end if;
end process;

-- Stuff to do on Falling edge of TSYSCLK
process(CLK8192,counter(9 downto 0)) 
begin
if (CLK8192'event and CLK8192='0') then
	lcounter <= counter;  -- save local copy of counter
	if (counter(9 downto 0)="0000000000") then
		RSYNC_REVA <= '1';  -- Generate RSYNC pulse
	else
		RSYNC_REVA <= '0';
	end if;
end if;
end process;

-- Handle Data input requests 
rxdata: process (ADDR(11 downto 10), rxq1out, rxq2out, RD, dbuf, statreg)
begin
	-- If in 32 bit space, Send data from the block we're not using
	if (RD = '0' and ADDR(11) = '0') then
		RRD <= '1'; -- Assert clock to output RAM
	   -- Mux DATA bus to proper RAMs
		if (dbuf = '1') then
			D <= rxq1out;
		else
			D <= rxq2out;
		end if;
		-- If in 8 bit space, return statreg
	elsif ((RD='0') and (ADDR(11 downto 10)="11")) then
		if (BE(1 downto 0) = "00") then	-- if C00, return status
			D(2 downto 0) <= statreg;
			D(31 downto 3) <= "ZZZZZZZZZZZZZZZZZZZZZZZZ00000";
		else -- if C01, return board id
			D(3 downto 0) <= NOT BOARDID;
			D(31 downto 4) <= "ZZZZZZZZZZZZZZZZZZZZZZZZ0000";
		end if;
		RRD <= '0';
	else -- If in outer space, Data bus should be tri-state
		D <= "ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ";
		RRD <= '0';
	end if;
end process rxdata;

-- rx serial loopback mux
rxmux: process(rser,xtser)
begin
	if (ctlreg(6)='1') then
		XRSER <= XTSER;
	else
		XRSER <= RSER;
	end if;
end process rxmux;

-- Handle Writing of RAMs when in 32 bit space
txdecode: process (WR, CLK, BE, dbuf, D, ADDR)
begin
	-- Make sure when we write to memory that we only 
	-- enable the clock on the actual RAM units if the
	-- top bit of address is '0', and is a full 32 bit access.
	if ((addr(11) = '0') and (BE="0000") and (WR='0') and (CLK='1')) then
		RWR <= '1';
	else
		RWR <= '0';
	end if;
end process txdecode;

-- Select the proper output 1.544 Mhz clock
clkmux: process(clkreg, RCLK)
begin
	if (clkreg = "001") then
		lclk <= RCLK(1);
	elsif (clkreg = "010") then
		lclk <= RCLK(2);
	elsif (clkreg = "011") then
	   lclk <= RCLK(3);
	elsif (clkreg = "100") then
		lclk <= RCLK(4);
	elsif (clkreg = "101") then
		lclk <= XSYNCIN;
	else
		lclk <= RCLK(0);
	end if;
	RCLKO <= lclk;
	XSYNCOUT <= lclk;
end process clkmux;

-- Stuff to do on positive edge of Local bus clock
process(CLK,ADDR(11 downto 10),RD,WR) 
begin
if (CLK'event and CLK='1') then -- On positive transition of clock
	if  ((WR='0' or RD='0') and ADDR(11 downto 10)="10") then -- If in our address range
		waitcnt <= waitcnt + 1; -- Bump state counter if in Dallas' address range
	else
		waitcnt <= "000"; -- Otherwise, leave reset
	end if;
	if (WR='0' and ADDR(11 downto 10)="11") then -- If to write to our configuration space
		if (ADDR(7 downto 2)="000000") then
			if (BE(1 downto 0)="11") then
				TEST2 <= D(0); -- Write to TEST2 pin (0xC03)
			elsif (BE(1 downto 0)="10") then
				ledreg <= D(7 downto 0); -- Write to the LED register (0xC02)
			elsif (BE(1 downto 0)="01") then
				ctlreg <= D(7 downto 0); -- Write to the ctlreg register (0xC01)
			else
				clkreg <= D(2 downto 0); -- Write to the clkreg register (0xC00)
			end if;
		end if;
		if (ADDR(7 downto 2)="000001") then
			if (BE(1 downto 0)="00") then
				ctlreg1 <= D(7 downto 0); -- Write to the ctlreg1 register (0xC04)
			end if;
		end if;
	end if;
	if ((statreg(1)='0') and (ctlreg(7)='1')) then -- if interrupt acked and de-asserted, ack the ack 
		ctlreg(7) <= '0';
	end if;
	if (ctlreg(0)='0') then -- if interrupts disabled, make sure ack is de-acked 
		ctlreg(7) <= '0';
	end if;
end if;
end process;

-- Generate Dallas Read and Write Signals and Wait states
process(CLK,ADDR(11 downto 8),RD,WR,waitcnt) 
begin
if ((WR='0' or RD='0') and ADDR(11 downto 10)="10") then  -- If during valid read or write
	-- Stuff for CS for Dallas Chips
	if (ADDR(9 downto 8)="00") then
		CS(4 downto 1) <= "1110";  -- Activate CS1
	end if;
	if (ADDR(9 downto 8)="01") then
		CS(4 downto 1) <= "1101";  -- Activate CS2
	end if;
	if (ADDR(9 downto 8)="10") then
		CS(4 downto 1) <= "1011";  -- Activate CS3
	end if;
	if (ADDR(9 downto 8)="11") then
		CS(4 downto 1) <= "0111";  -- Activate CS4
	end if;
	if (waitcnt <= "100") then -- An intermediate cycle (before ready)
		if (WR='0') then -- If a write cycle, output it
			DWR <= '0';
		else
			DWR <= '1';
		end if;
		if (RD='0') then -- If a read cycle, output it
			DRD <= '0';
		else
			DRD <= '1';
		end if;
	end if;
	if ((waitcnt = "011") and (CLK='0')) then -- If were at 4, were ready, and this will be real one
		READY <= '0';
	end if;
	if (waitcnt > "100") then -- Count is greater then 4, time to reset everything
		READY <= '1';
		DWR <= '1';
		DRD <= '1';
	end if;
else  -- Not in read or write in the appropriate range, reset the stuff
	READY <= '1';
	DWR <= '1';
	DRD <= '1';	
	CS(4 downto 1) <= "1111"; -- No CS outputs
end if;
if (waitcnt="100" and CLK='1' and WR='0') then -- De-activate the DWR signal at the final half cycle
	DWR <= '1';
	DWR <= '1';
end if;
if ((WR='0' or RD='0') and ADDR(11 downto 10)/="10") then  -- If during not valid read or write
		READY <= '0';  -- Dont hang the bus for them
end if;
end process;

-- MUX for Frame sync lines depending upon part revision
process(tssync_local,rsync_reva,ctlreg1(0 downto 0))
begin
	if (ctlreg1(0 downto 0) = "0") then -- Do output for Rev. A part
		TSSYNC <= TSSYNC_LOCAL;
		RSYNC <= RSYNC_REVA;
	else
		TSSYNC <= TSSYNC_LOCAL;
		RSYNC <= TSSYNC_LOCAL;
	end if;
end process;
end behavioral;
