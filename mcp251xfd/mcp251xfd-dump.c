// SPDX-License-Identifier: GPL-2.0
//
// Microchip MCP251xFD Family CAN controller debug tool
//
// Copyright (c) 2019, 2020, 2021 Pengutronix,
//               Marc Kleine-Budde <kernel@pengutronix.de>
//

#include <linux/kernel.h>

#include "mcp251xfd-dump-userspace.h"

struct mcp251xfd_dump_regs_fifo {
	u32 con;
	u32 sta;
	u32 ua;
};

struct mcp251xfd_dump_regs_filter {
	u32 obj;
	u32 mask;
};

struct mcp251xfd_dump_regs {
	u32 con;
	u32 nbtcfg;
	u32 dbtcfg;
	u32 tdc;
	u32 tbc;
	u32 tscon;
	u32 vec;
	u32 intf;
	u32 rxif;
	u32 txif;
	u32 rxovif;
	u32 txatif;
	u32 txreq;
	u32 trec;
	u32 bdiag0;
	u32 bdiag1;
	union {
		struct {
			u32 tefcon;
			u32 tefsta;
			u32 tefua;
		};
		struct mcp251xfd_dump_regs_fifo tef;
	};
	u32 reserved0;
	union {
		struct {
			struct mcp251xfd_dump_regs_fifo txq;
			struct mcp251xfd_dump_regs_fifo tx_fifo;
			struct mcp251xfd_dump_regs_fifo rx_fifo;
		};
		struct mcp251xfd_dump_regs_fifo fifo[32];
	};
	u32 fltcon[8];
	struct mcp251xfd_dump_regs_filter filter[32];
};

struct mcp251xfd_dump_ram {
	u8 ram[MCP251XFD_RAM_SIZE];
};

struct mcp251xfd_dump_regs_mcp251xfd {
	u32 osc;
	u32 iocon;
	u32 crc;
	u32 ecccon;
	u32 eccstat;
	u32 devid;
};

#define __dump_bit(val, prefix, bit, desc)	       \
	pr_info("%16s   %s\t\t%s\n", __stringify(bit), \
		(val) & prefix##_##bit ? "x" : " ", desc)

#define __dump_mask(val, prefix, mask, fmt, desc) \
	pr_info("%16s = " fmt "\t\t%s\n", \
		__stringify(mask), \
		FIELD_GET(prefix##_##mask##_MASK, (val)), \
		desc)

static void mcp251xfd_dump_reg_con(const struct mcp251xfd_priv *priv, u32 val, u16 addr)
{
	pr_info("CON: con(0x%03x)=0x%08x\n", addr, val);

	__dump_mask(val, MCP251XFD_REG_CON, TXBWS, "0x%02lx", "Transmit Bandwidth Sharing");
	__dump_bit(val, MCP251XFD_REG_CON, ABAT, "Abort All Pending Transmissions");
	__dump_mask(val, MCP251XFD_REG_CON, REQOP, "0x%02lx", "Request Operation Mode");
	__dump_mask(val, MCP251XFD_REG_CON, OPMOD, "0x%02lx", "Operation Mode Status");
	__dump_bit(val, MCP251XFD_REG_CON, TXQEN, "Enable Transmit Queue");
	__dump_bit(val, MCP251XFD_REG_CON, STEF, "Store in Transmit Event FIFO");
	__dump_bit(val, MCP251XFD_REG_CON, SERR2LOM, "Transition to Listen Only Mode on System Error");
	__dump_bit(val, MCP251XFD_REG_CON, ESIGM, "Transmit ESI in Gateway Mode");
	__dump_bit(val, MCP251XFD_REG_CON, RTXAT, "Restrict Retransmission Attempts");
	__dump_bit(val, MCP251XFD_REG_CON, BRSDIS, "Bit Rate Switching Disable");
	__dump_bit(val, MCP251XFD_REG_CON, BUSY, "CAN Module is Busy");
	__dump_mask(val, MCP251XFD_REG_CON, WFT, "0x%02lx", "Selectable Wake-up Filter Time");
	__dump_bit(val, MCP251XFD_REG_CON, WAKFIL, "Enable CAN Bus Line Wake-up Filter");
	__dump_bit(val, MCP251XFD_REG_CON, PXEDIS, "Protocol Exception Event Detection Disabled");
	__dump_bit(val, MCP251XFD_REG_CON, ISOCRCEN, "Enable ISO CRC in CAN FD Frames");
	__dump_mask(val, MCP251XFD_REG_CON, DNCNT, "0x%02lx", "Device Net Filter Bit Number");
}

static void mcp251xfd_dump_reg_nbtcfg(const struct mcp251xfd_priv *priv, u32 val, u16 addr)
{
	pr_info("NBTCFG: nbtcfg(0x%03x)=0x%08x\n", addr, val);

	__dump_mask(val, MCP251XFD_REG_NBTCFG, BRP, "%3lu", "Baud Rate Prescaler");
	__dump_mask(val, MCP251XFD_REG_NBTCFG, TSEG1, "%3lu", "Time Segment 1 (Propagation Segment + Phase Segment 1)");
	__dump_mask(val, MCP251XFD_REG_NBTCFG, TSEG2, "%3lu", "Time Segment 2 (Phase Segment 2)");
	__dump_mask(val, MCP251XFD_REG_NBTCFG, SJW, "%3lu", "Synchronization Jump Width");
}

static void mcp251xfd_dump_reg_dbtcfg(const struct mcp251xfd_priv *priv, u32 val, u16 addr)
{
	pr_info("DBTCFG: dbtcfg(0x%03x)=0x%08x\n", addr, val);

	__dump_mask(val, MCP251XFD_REG_DBTCFG, BRP, "%3lu", "Baud Rate Prescaler");
	__dump_mask(val, MCP251XFD_REG_DBTCFG, TSEG1, "%3lu", "Time Segment 1 (Propagation Segment + Phase Segment 1)");
	__dump_mask(val, MCP251XFD_REG_DBTCFG, TSEG2, "%3lu", "Time Segment 2 (Phase Segment 2)");
	__dump_mask(val, MCP251XFD_REG_DBTCFG, SJW, "%3lu", "Synchronization Jump Width");
}

static void mcp251xfd_dump_reg_tdc(const struct mcp251xfd_priv *priv, u32 val, u16 addr)
{
	pr_info("TDC: tdc(0x%03x)=0x%08x\n", addr, val);

	__dump_bit(val, MCP251XFD_REG_TDC, EDGFLTEN, "Enable Edge Filtering during Bus Integration state");
	__dump_bit(val, MCP251XFD_REG_TDC, SID11EN, "Enable 12-Bit SID in CAN FD Base Format Messages");
	__dump_mask(val, MCP251XFD_REG_TDC, TDCMOD, "0x%02lx", "Transmitter Delay Compensation Mode");
	__dump_mask(val, MCP251XFD_REG_TDC, TDCO, "0x%02lx", "Transmitter Delay Compensation Offset");
	__dump_mask(val, MCP251XFD_REG_TDC, TDCV, "0x%02lx", "Transmitter Delay Compensation Value");
}

static void mcp251xfd_dump_reg_tbc(const struct mcp251xfd_priv *priv, u32 val, u16 addr)
{
	pr_info("TBC: tbc(0x%03x)=0x%08x\n", addr, val);
}

static void mcp251xfd_dump_reg_vec(const struct mcp251xfd_priv *priv, u32 val, u16 addr)
{
	u8 rx_code, tx_code, i_code;

	pr_info("VEC: vec(0x%03x)=0x%08x\n", addr, val);

	rx_code = FIELD_GET(MCP251XFD_REG_VEC_RXCODE_MASK, val);
	tx_code = FIELD_GET(MCP251XFD_REG_VEC_TXCODE_MASK, val);
	i_code = FIELD_GET(MCP251XFD_REG_VEC_ICODE_MASK, val);

	pr_info("\trxcode: ");
	if (rx_code == 0x40)
		pr_cont("No Interrupt");
	else if (rx_code < 0x20)
		pr_cont("FIFO %u", rx_code);
	else
		pr_cont("Reserved");
	pr_cont(" (0x%02x)\n", rx_code);

	pr_info("\ttxcode: ");
	if (tx_code == 0x40)
		pr_cont("No Interrupt");
	else if (tx_code < 0x20)
		pr_cont("FIFO %u", tx_code);
	else
		pr_cont("Reserved");
	pr_cont(" (0x%02x)\n", tx_code);

	pr_info("\ticode: ");
	if (i_code == 0x4a)
		pr_cont("Transmit Attempt Interrupt");
	else if (i_code == 0x49)
		pr_cont("Transmit Event FIFO Interrupt");
	else if (i_code == 0x48)
		pr_cont("Invalid Message Occurred");
	else if (i_code == 0x47)
		pr_cont("Operation Mode Changed");
	else if (i_code == 0x46)
		pr_cont("TBC Overflow");
	else if (i_code == 0x45)
		pr_cont("RX/TX MAB Overflow/Underflow");
	else if (i_code == 0x44)
		pr_cont("Address Error Interrupt");
	else if (i_code == 0x43)
		pr_cont("Receive FIFO Overflow Interrupt");
	else if (i_code == 0x42)
		pr_cont("Wake-up Interrupt");
	else if (i_code == 0x41)
		pr_cont("Error Interrupt");
	else if (i_code == 0x40)
		pr_cont("No Interrupt");
	else if (i_code < 0x20)
		pr_cont("FIFO %u", i_code);
	else
		pr_cont("Reserved");
	pr_cont(" (0x%02x)\n", i_code);
}

#define __dump_int(val, bit, desc) \
	pr_info("\t" __stringify(bit) "\t%s\t%s\t%s\t%s\n", \
		 (val) & MCP251XFD_REG_INT_##bit##E ? "x" : "", \
		 (val) & MCP251XFD_REG_INT_##bit##F ? "x" : "", \
		 FIELD_GET(MCP251XFD_REG_INT_IF_MASK, val) & \
		 FIELD_GET(MCP251XFD_REG_INT_IE_MASK, val) & \
		 MCP251XFD_REG_INT_##bit##F ? "x" : "", \
		 desc)

static void mcp251xfd_dump_reg_intf(const struct mcp251xfd_priv *priv, u32 val, u16 addr)
{
	pr_info("INT: intf(0x%03x)=0x%08x\n", addr, val);

	pr_info("\t\tIE\tIF\tIE & IF\n");
	__dump_int(val, IVMI, "Invalid Message Interrupt");
	__dump_int(val, WAKI, "Bus Wake Up Interrupt");
	__dump_int(val, CERRI, "CAN Bus Error Interrupt");
	__dump_int(val, SERRI, "System Error Interrupt");
	__dump_int(val, RXOVI, "Receive FIFO Overflow Interrupt");
	__dump_int(val, TXATI, "Transmit Attempt Interrupt");
	__dump_int(val, SPICRCI, "SPI CRC Error Interrupt");
	__dump_int(val, ECCI, "ECC Error Interrupt");
	__dump_int(val, TEFI, "Transmit Event FIFO Interrupt");
	__dump_int(val, MODI, "Mode Change Interrupt");
	__dump_int(val, TBCI, "Time Base Counter Interrupt");
	__dump_int(val, RXI, "Receive FIFO Interrupt");
	__dump_int(val, TXI, "Transmit FIFO Interrupt");
}

#undef __dump_int

#define __create_dump_fifo_bitmask(fifo, name, description) \
static void mcp251xfd_dump_reg_##fifo(const struct mcp251xfd_priv *priv, u32 val, u16 addr) \
{ \
	int i; \
\
	pr_info(__stringify(name) ": " __stringify(fifo) "(0x%03x)=0x%08x\n", addr, val); \
	pr_info(description ":\n"); \
	if (!val) { \
		pr_info("\t\t-none-\n"); \
		return; \
	} \
\
	pr_info("\t\t"); \
	for (i = 0; i < sizeof(val); i++) { \
		if (val & BIT(i)) \
			pr_cont("%d ", i); \
	} \
\
	pr_cont("\n"); \
}

__create_dump_fifo_bitmask(rxif, RXIF, "Receive FIFO Interrupt Pending");
__create_dump_fifo_bitmask(rxovif, RXOVIF, "Receive FIFO Overflow Interrupt Pending");
__create_dump_fifo_bitmask(txif, TXIF, "Transmit FIFO Interrupt Pending");
__create_dump_fifo_bitmask(txatif, TXATIF, "Transmit FIFO Attempt Interrupt Pending");
__create_dump_fifo_bitmask(txreq, TXREQ, "Message Send Request");

#undef __create_dump_fifo_bitmask

static void mcp251xfd_dump_reg_trec(const struct mcp251xfd_priv *priv, u32 val, u16 addr)
{
	pr_info("TREC: trec(0x%03x)=0x%08x\n", addr, val);

	__dump_bit(val, MCP251XFD_REG_TREC, TXBO, "Transmitter in Bus Off State");
	__dump_bit(val, MCP251XFD_REG_TREC, TXBP, "Transmitter in Error Passive State");
	__dump_bit(val, MCP251XFD_REG_TREC, RXBP, "Receiver in Error Passive State");
	__dump_bit(val, MCP251XFD_REG_TREC, TXWARN, "Transmitter in Error Warning State");
	__dump_bit(val, MCP251XFD_REG_TREC, RXWARN, "Receiver in Error Warning State");
	__dump_bit(val, MCP251XFD_REG_TREC, EWARN, "Transmitter or Receiver is in Error Warning State");

	__dump_mask(val, MCP251XFD_REG_TREC, TEC, "%3lu", "Transmit Error Counter");
	__dump_mask(val, MCP251XFD_REG_TREC, REC, "%3lu", "Receive Error Counter");
}

static void mcp251xfd_dump_reg_bdiag0(const struct mcp251xfd_priv *priv, u32 val, u16 addr)
{
	pr_info("BDIAG0: bdiag0(0x%03x)=0x%08x\n", addr, val);

	__dump_mask(val, MCP251XFD_REG_BDIAG0, DTERRCNT, "%3lu", "Data Bit Rate Transmit Error Counter");
	__dump_mask(val, MCP251XFD_REG_BDIAG0, DRERRCNT, "%3lu", "Data Bit Rate Receive Error Counter");
	__dump_mask(val, MCP251XFD_REG_BDIAG0, NTERRCNT, "%3lu", "Nominal Bit Rate Transmit Error Counter");
	__dump_mask(val, MCP251XFD_REG_BDIAG0, NRERRCNT, "%3lu", "Nominal Bit Rate Receive Error Counter");
}

static void mcp251xfd_dump_reg_bdiag1(const struct mcp251xfd_priv *priv, u32 val, u16 addr)
{
	pr_info("BDIAG1: bdiag1(0x%03x)=0x%08x\n", addr, val);

	__dump_bit(val, MCP251XFD_REG_BDIAG1, DLCMM, "DLC Mismatch");
	__dump_bit(val, MCP251XFD_REG_BDIAG1, ESI, "ESI flag of a received CAN FD message was set");
	__dump_bit(val, MCP251XFD_REG_BDIAG1, DCRCERR, "Data CRC Error");
	__dump_bit(val, MCP251XFD_REG_BDIAG1, DSTUFERR, "Data Bit Stuffing Error");
	__dump_bit(val, MCP251XFD_REG_BDIAG1, DFORMERR, "Data Format Error");
	__dump_bit(val, MCP251XFD_REG_BDIAG1, DBIT1ERR, "Data BIT1 Error");
	__dump_bit(val, MCP251XFD_REG_BDIAG1, DBIT0ERR, "Data BIT0 Error");
	__dump_bit(val, MCP251XFD_REG_BDIAG1, TXBOERR, "Device went to bus-off (and auto-recovered)");
	__dump_bit(val, MCP251XFD_REG_BDIAG1, NCRCERR, "CRC Error");
	__dump_bit(val, MCP251XFD_REG_BDIAG1, NSTUFERR, "Bit Stuffing Error");
	__dump_bit(val, MCP251XFD_REG_BDIAG1, NFORMERR, "Format Error");
	__dump_bit(val, MCP251XFD_REG_BDIAG1, NACKERR, "Transmitted message was not acknowledged");
	__dump_bit(val, MCP251XFD_REG_BDIAG1, NBIT1ERR, "Bit1 Error");
	__dump_bit(val, MCP251XFD_REG_BDIAG1, NBIT0ERR, "Bit0 Error");
	__dump_mask(val, MCP251XFD_REG_BDIAG1, EFMSGCNT, "%3lu", "Error Free Message Counter");
}

static void mcp251xfd_dump_reg_osc(const struct mcp251xfd_priv *priv, u32 val, u16 addr)
{
	pr_info("OSC: osc(0x%03x)=0x%08x\n", addr, val);

	__dump_bit(val, MCP251XFD_REG_OSC, SCLKRDY, "Synchronized SCLKDIV");
	__dump_bit(val, MCP251XFD_REG_OSC, OSCRDY, "Clock Ready");
	__dump_bit(val, MCP251XFD_REG_OSC, PLLRDY, "PLL Ready");
	__dump_mask(val, MCP251XFD_REG_OSC, CLKODIV, "0x%02lu", "Clock Output Divisor");
	__dump_bit(val, MCP251XFD_REG_OSC, SCLKDIV, "System Clock Divisor");
	__dump_bit(val, MCP251XFD_REG_OSC, LPMEN, "Low Power Mode (LPM) Enable (MCP2518FD only)");
	__dump_bit(val, MCP251XFD_REG_OSC, OSCDIS, "Clock (Oscillator) Disable");
	__dump_bit(val, MCP251XFD_REG_OSC, PLLEN, "PLL Enable");
}

static void mcp251xfd_dump_reg_iocon(const struct mcp251xfd_priv *priv, u32 val, u16 addr)
{
	pr_info("IOCON: iocon(0x%03x)=0x%08x\n", addr, val);

	__dump_bit(val, MCP251XFD_REG_IOCON, INTOD, "Interrupt pins Open Drain Mode (0: Push/Pull Output, 1: Open Drain Output)");
	__dump_bit(val, MCP251XFD_REG_IOCON, SOF, "Start-Of-Frame signal (0: Clock on CLKO pin, 1: SOF signal on CLKO pin)");
	__dump_bit(val, MCP251XFD_REG_IOCON, TXCANOD, "TXCAN Open Drain Mode (0: Push/Pull Output, 1: Open Drain Output)");
	__dump_bit(val, MCP251XFD_REG_IOCON, PM1, "GPIO Pin Mode (0: Interrupt Pin INT1 (RXIF), 1: Pin is used as GPIO1)");
	__dump_bit(val, MCP251XFD_REG_IOCON, PM0, "GPIO Pin Mode (0: Interrupt Pin INT0 (TXIF), 1: Pin is used as GPIO0)");
	__dump_bit(val, MCP251XFD_REG_IOCON, GPIO1, "GPIO1 Status");
	__dump_bit(val, MCP251XFD_REG_IOCON, GPIO0, "GPIO0 Status");
	__dump_bit(val, MCP251XFD_REG_IOCON, LAT1, "GPIO1 Latch");
	__dump_bit(val, MCP251XFD_REG_IOCON, LAT0, "GPIO0 Latch");
	__dump_bit(val, MCP251XFD_REG_IOCON, XSTBYEN, "Enable Transceiver Standby Pin Control");
	__dump_bit(val, MCP251XFD_REG_IOCON, TRIS1, "GPIO1 Data Direction (0: Output Pin, 1: Input Pin)");
	__dump_bit(val, MCP251XFD_REG_IOCON, TRIS0, "GPIO0 Data Direction (0: Output Pin, 1: Input Pin)");
}

static void mcp251xfd_dump_reg_tefcon(const struct mcp251xfd_priv *priv, u32 val, u16 addr)
{
	pr_info("TEFCON: tefcon(0x%03x)=0x%08x\n", addr, val);

	__dump_mask(val, MCP251XFD_REG_TEFCON, FSIZE, "%3lu", "FIFO Size");
	__dump_bit(val, MCP251XFD_REG_TEFCON, FRESET, "FIFO Reset");
	__dump_bit(val, MCP251XFD_REG_TEFCON, UINC, "Increment Tail");
	__dump_bit(val, MCP251XFD_REG_TEFCON, TEFTSEN, "Transmit Event FIFO Time Stamp Enable");
	__dump_bit(val, MCP251XFD_REG_TEFCON, TEFOVIE, "Transmit Event FIFO Overflow Interrupt Enable");
	__dump_bit(val, MCP251XFD_REG_TEFCON, TEFFIE, "Transmit Event FIFO Full Interrupt Enable");
	__dump_bit(val, MCP251XFD_REG_TEFCON, TEFHIE, "Transmit Event FIFO Half Full Interrupt Enable");
	__dump_bit(val, MCP251XFD_REG_TEFCON, TEFNEIE, "Transmit Event FIFO Not Empty Interrupt Enable");
}

static void mcp251xfd_dump_reg_tefsta(const struct mcp251xfd_priv *priv, u32 val, u16 addr)
{
	pr_info("TEFSTA: tefsta(0x%03x)=0x%08x\n", addr, val);

	__dump_bit(val, MCP251XFD_REG_TEFSTA, TEFOVIF, "Transmit Event FIFO Overflow Interrupt Flag");
	__dump_bit(val, MCP251XFD_REG_TEFSTA, TEFFIF, "Transmit Event FIFO Full Interrupt Flag (0: not full)");
	__dump_bit(val, MCP251XFD_REG_TEFSTA, TEFHIF, "Transmit Event FIFO Half Full Interrupt Flag (0: < half full)");
	__dump_bit(val, MCP251XFD_REG_TEFSTA, TEFNEIF, "Transmit Event FIFO Not Empty Interrupt Flag (0: empty)");
}

static void mcp251xfd_dump_reg_tefua(const struct mcp251xfd_priv *priv, u32 val, u16 addr)
{
	pr_info("TEFUA: tefua(0x%03x)=0x%08x\n", addr, val);
}

static void mcp251xfd_dump_reg_fifocon(const struct mcp251xfd_priv *priv, u32 val, u16 addr)
{
	pr_info("FIFOCON: fifocon(0x%03x)=0x%08x\n", addr, val);

	__dump_mask(val, MCP251XFD_REG_FIFOCON, PLSIZE, "%3lu", "Payload Size");
	__dump_mask(val, MCP251XFD_REG_FIFOCON, FSIZE, "%3lu", "FIFO Size");
	__dump_mask(val, MCP251XFD_REG_FIFOCON, TXAT, "%3lu", "Retransmission Attempts");
	__dump_mask(val, MCP251XFD_REG_FIFOCON, TXPRI, "%3lu", "Message Transmit Priority");
	__dump_bit(val, MCP251XFD_REG_FIFOCON, FRESET, "FIFO Reset");
	__dump_bit(val, MCP251XFD_REG_FIFOCON, TXREQ, "Message Send Request");
	__dump_bit(val, MCP251XFD_REG_FIFOCON, UINC, "Increment Head/Tail");
	__dump_bit(val, MCP251XFD_REG_FIFOCON, TXEN, "TX/RX FIFO Selection (0: RX, 1: TX)");
	__dump_bit(val, MCP251XFD_REG_FIFOCON, RTREN, "Auto RTR Enable");
	__dump_bit(val, MCP251XFD_REG_FIFOCON, RXTSEN, "Received Message Time Stamp Enable");
	__dump_bit(val, MCP251XFD_REG_FIFOCON, TXATIE, "Transmit Attempts Exhausted Interrupt Enable");
	__dump_bit(val, MCP251XFD_REG_FIFOCON, RXOVIE, "Overflow Interrupt Enable");
	__dump_bit(val, MCP251XFD_REG_FIFOCON, TFERFFIE, "Transmit/Receive FIFO Empty/Full Interrupt Enable");
	__dump_bit(val, MCP251XFD_REG_FIFOCON, TFHRFHIE, "Transmit/Receive FIFO Half Empty/Half Full Interrupt Enable");
	__dump_bit(val, MCP251XFD_REG_FIFOCON, TFNRFNIE, "Transmit/Receive FIFO Not Full/Not Empty Interrupt Enable");
}

static void mcp251xfd_dump_reg_fifosta(const struct mcp251xfd_priv *priv, u32 val, u16 addr)
{
	pr_info("FIFOSTA: fifosta(0x%03x)=0x%08x\n", addr, val);

	__dump_mask(val, MCP251XFD_REG_FIFOSTA, FIFOCI, "%3lu", "FIFO Message Index");
	__dump_bit(val, MCP251XFD_REG_FIFOSTA, TXABT, "Message Aborted Status (0: completed successfully, 1: aborted)");
	__dump_bit(val, MCP251XFD_REG_FIFOSTA, TXLARB, "Message Lost Arbitration Status");
	__dump_bit(val, MCP251XFD_REG_FIFOSTA, TXERR, "Error Detected During Transmission");
	__dump_bit(val, MCP251XFD_REG_FIFOSTA, TXATIF, "Transmit Attempts Exhausted Interrupt Pending");
	__dump_bit(val, MCP251XFD_REG_FIFOSTA, RXOVIF, "Receive FIFO Overflow Interrupt Flag");
	__dump_bit(val, MCP251XFD_REG_FIFOSTA, TFERFFIF, "Transmit/Receive FIFO Empty/Full Interrupt Flag");
	__dump_bit(val, MCP251XFD_REG_FIFOSTA, TFHRFHIF, "Transmit/Receive FIFO Half Empty/Half Full Interrupt Flag");
	__dump_bit(val, MCP251XFD_REG_FIFOSTA, TFNRFNIF, "Transmit/Receive FIFO Not Full/Not Empty Interrupt Flag");
}

static void mcp251xfd_dump_reg_fifoua(const struct mcp251xfd_priv *priv, u32 val, u16 addr)
{
	pr_info("FIFOUA: fifoua(0x%03x)=0x%08x\n", addr, val);
}

#define __dump_call(regs, val) \
do { \
	mcp251xfd_dump_reg_##val(priv, (regs)->val, \
				 (u16)(offsetof(typeof(*(regs)), val) + \
				       (sizeof(*(regs)) == sizeof(struct mcp251xfd_dump_regs) ? \
					0 : MCP251XFD_REG_OSC))); \
	pr_info("\n"); \
} while (0)

#define __dump_call_fifo(reg, val) \
do { \
	mcp251xfd_dump_reg_##reg(priv, regs->val, (u16)offsetof(typeof(*regs), val)); \
	pr_info("\n"); \
} while (0)

static void
mcp251xfd_dump_regs(const struct mcp251xfd_priv *priv,
		    const struct mcp251xfd_dump_regs *regs,
		    const struct mcp251xfd_dump_regs_mcp251xfd *regs_mcp251xfd)
{
	netdev_info(priv->ndev, "-------------------- register dump --------------------\n");
	__dump_call(regs, con);
	__dump_call(regs, nbtcfg);
	__dump_call(regs, dbtcfg);
	__dump_call(regs, tdc);
	__dump_call(regs, tbc);
	__dump_call(regs, vec);
	__dump_call(regs, intf);
	__dump_call(regs, rxif);
	__dump_call(regs, rxovif);
	__dump_call(regs, txif);
	__dump_call(regs, txatif);
	__dump_call(regs, txreq);
	__dump_call(regs, trec);
	__dump_call(regs, bdiag0);
	__dump_call(regs, bdiag1);
	__dump_call(regs_mcp251xfd, osc);
	__dump_call(regs_mcp251xfd, iocon);
	pr_info("-------------------- TEF --------------------\n");
	__dump_call(regs, tefcon);
	__dump_call(regs, tefsta);
	__dump_call(regs, tefua);
	pr_info("-------------------- TX_FIFO --------------------\n");
	__dump_call_fifo(fifocon, fifo[MCP251XFD_TX_FIFO].con);
	__dump_call_fifo(fifosta, fifo[MCP251XFD_TX_FIFO].sta);
	__dump_call_fifo(fifoua, fifo[MCP251XFD_TX_FIFO].ua);
	pr_info(" -------------------- RX_FIFO --------------------\n");
	__dump_call_fifo(fifocon, fifo[MCP251XFD_RX_FIFO(0)].con);
	__dump_call_fifo(fifosta, fifo[MCP251XFD_RX_FIFO(0)].sta);
	__dump_call_fifo(fifoua, fifo[MCP251XFD_RX_FIFO(0)].ua);
	netdev_info(priv->ndev, "------------------------- end -------------------------\n");
}

#undef __dump_call
#undef __dump_call_fifo

static u8 mcp251xfd_dump_get_fifo_size(const struct mcp251xfd_priv *priv, const struct mcp251xfd_dump_regs *regs, u32 fifo_con)
{
	u8 obj_size;

	obj_size = FIELD_GET(MCP251XFD_REG_FIFOCON_PLSIZE_MASK, fifo_con);
	switch (obj_size) {
	case MCP251XFD_REG_FIFOCON_PLSIZE_8:
		return 8;
	case MCP251XFD_REG_FIFOCON_PLSIZE_12:
		return 12;
	case MCP251XFD_REG_FIFOCON_PLSIZE_16:
		return 16;
	case MCP251XFD_REG_FIFOCON_PLSIZE_20:
		return 20;
	case MCP251XFD_REG_FIFOCON_PLSIZE_24:
		return 24;
	case MCP251XFD_REG_FIFOCON_PLSIZE_32:
		return 32;
	case MCP251XFD_REG_FIFOCON_PLSIZE_48:
		return 48;
	case MCP251XFD_REG_FIFOCON_PLSIZE_64:
		return 64;
	}

	return 0;
}

static u8 mcp251xfd_dump_get_fifo_obj_num(const struct mcp251xfd_priv *priv, const struct mcp251xfd_dump_regs *regs, u32 fifo_con)
{
	u8 obj_num;

	obj_num = FIELD_GET(MCP251XFD_REG_FIFOCON_FSIZE_MASK, fifo_con);

	return obj_num + 1;
}

static void mcp251xfd_dump_ram_fifo_obj_data(const struct mcp251xfd_priv *priv, const u8 *data, u8 dlc)
{
	int i;
	u8 len;

	len = can_dlc2len(get_canfd_dlc(dlc));

	if (!len) {
		pr_info("%16s = -none-\n", "data");
		return;
	}

	for (i = 0; i < len; i++) {
		if ((i % 8) == 0) {
			if (i == 0)
				pr_info("%16s = %02x", "data", data[i]);
			else
				pr_info("                   %02x", data[i]);
		} else if ((i % 4) == 0) {
			pr_cont("  %02x", data[i]);
		} else if ((i % 8) == 7) {
			pr_cont(" %02x\n", data[i]);
		} else {
			pr_cont(" %02x", data[i]);
		}
	}

	if (i % 8)
		pr_cont("\n");
}

/* TEF */

static u8
mcp251xfd_dump_get_tef_obj_num(const struct mcp251xfd_priv *priv,
			       const struct mcp251xfd_dump_regs *regs)
{
	return mcp251xfd_dump_get_fifo_obj_num(priv, regs, regs->tef.con);
}

static u8
mcp251xfd_dump_get_tef_tail(const struct mcp251xfd_priv *priv,
			    const struct mcp251xfd_dump_regs *regs)
{
	return regs->tefua / sizeof(struct mcp251xfd_hw_tef_obj);
}

static u16
mcp251xfd_dump_get_tef_obj_rel_addr(const struct mcp251xfd_priv *priv,
				    u8 n)
{
	return sizeof(struct mcp251xfd_hw_tef_obj) * n;
}

static u16
mcp251xfd_dump_get_tef_obj_addr(const struct mcp251xfd_priv *priv,
				u8 n)
{
	return mcp251xfd_dump_get_tef_obj_rel_addr(priv, n) +
		MCP251XFD_RAM_START;
}

/* TX */

static u8
mcp251xfd_dump_get_tx_obj_size(const struct mcp251xfd_priv *priv,
			       const struct mcp251xfd_dump_regs *regs)
{
	return sizeof(struct mcp251xfd_hw_tx_obj_can) -
		sizeof_field(struct mcp251xfd_hw_tx_obj_can, data) +
		mcp251xfd_dump_get_fifo_size(priv, regs, regs->tx_fifo.con);
}

static u8
mcp251xfd_dump_get_tx_obj_num(const struct mcp251xfd_priv *priv,
			      const struct mcp251xfd_dump_regs *regs)
{
	return mcp251xfd_dump_get_fifo_obj_num(priv, regs, regs->tx_fifo.con);
}

static u16
mcp251xfd_dump_get_tx_obj_rel_addr(const struct mcp251xfd_priv *priv,
				   const struct mcp251xfd_dump_regs *regs,
				   u8 n)
{
	return mcp251xfd_dump_get_tef_obj_rel_addr(priv, mcp251xfd_dump_get_tef_obj_num(priv, regs)) +
		mcp251xfd_dump_get_tx_obj_size(priv, regs) * n;
}

static u16
mcp251xfd_dump_get_tx_obj_addr(const struct mcp251xfd_priv *priv,
			       const struct mcp251xfd_dump_regs *regs, u8 n)
{
	return mcp251xfd_dump_get_tx_obj_rel_addr(priv, regs, n) +
		MCP251XFD_RAM_START;
}

static u8
mcp251xfd_dump_get_tx_tail(const struct mcp251xfd_priv *priv,
			   const struct mcp251xfd_dump_regs *regs)
{
	return (regs->fifo[MCP251XFD_TX_FIFO].ua -
		mcp251xfd_dump_get_tx_obj_rel_addr(priv, regs, 0)) /
		mcp251xfd_dump_get_tx_obj_size(priv, regs);
}

static u8
mcp251xfd_dump_get_tx_head(const struct mcp251xfd_priv *priv,
			   const struct mcp251xfd_dump_regs *regs)
{
	return FIELD_GET(MCP251XFD_REG_FIFOSTA_FIFOCI_MASK,
			 regs->fifo[MCP251XFD_TX_FIFO].sta);
}

/* RX */

static u8
mcp251xfd_dump_get_rx_obj_size(const struct mcp251xfd_priv *priv,
			       const struct mcp251xfd_dump_regs *regs)
{
	return sizeof(struct mcp251xfd_hw_rx_obj_can) -
		sizeof_field(struct mcp251xfd_hw_rx_obj_can, data) +
		mcp251xfd_dump_get_fifo_size(priv, regs, regs->rx_fifo.con);
}

static u8
mcp251xfd_dump_get_rx_obj_num(const struct mcp251xfd_priv *priv,
			      const struct mcp251xfd_dump_regs *regs)
{
	return mcp251xfd_dump_get_fifo_obj_num(priv, regs, regs->rx_fifo.con);
}

static u16
mcp251xfd_dump_get_rx_obj_rel_addr(const struct mcp251xfd_priv *priv,
				   const struct mcp251xfd_dump_regs *regs, u8 n)
{
	return mcp251xfd_dump_get_tx_obj_rel_addr(priv, regs, mcp251xfd_dump_get_tx_obj_num(priv, regs)) +
		mcp251xfd_dump_get_rx_obj_size(priv, regs) * n;
}

static u16
mcp251xfd_dump_get_rx_obj_addr(const struct mcp251xfd_priv *priv,
			       const struct mcp251xfd_dump_regs *regs, u8 n)
{
	return mcp251xfd_dump_get_rx_obj_rel_addr(priv, regs, n) + MCP251XFD_RAM_START;
}

static u8
mcp251xfd_dump_get_rx_tail(const struct mcp251xfd_priv *priv,
			   const struct mcp251xfd_dump_regs *regs)
{
	return (regs->fifo[MCP251XFD_RX_FIFO(0)].ua -
		mcp251xfd_dump_get_rx_obj_rel_addr(priv, regs, 0)) /
		mcp251xfd_dump_get_rx_obj_size(priv, regs);
}

static u8
mcp251xfd_dump_get_rx_head(const struct mcp251xfd_priv *priv,
			   const struct mcp251xfd_dump_regs *regs)
{
	return FIELD_GET(MCP251XFD_REG_FIFOSTA_FIFOCI_MASK, regs->fifo[MCP251XFD_RX_FIFO(0)].sta);
}

/* dump TEF */

static void
mcp251xfd_dump_ram_tef_obj_one(const struct mcp251xfd_priv *priv,
			       const struct mcp251xfd_dump_regs *regs,
			       const struct mcp251xfd_ring *tef,
			       const struct mcp251xfd_hw_tef_obj *hw_tef_obj,
			       u8 n)
{
	pr_info("TEF Object: 0x%02x (0x%03x)%s%s%s%s%s\n",
		n, mcp251xfd_dump_get_tef_obj_addr(priv, n),
		mcp251xfd_get_ring_head(tef) == n ? "  priv-HEAD" : "",
		mcp251xfd_dump_get_tef_tail(priv, regs) == n ? "  chip-TAIL" : "",
		mcp251xfd_get_ring_tail(tef) == n ? "  priv-TAIL" : "",
		(mcp251xfd_dump_get_tef_tail(priv, regs) == n ?
		 ((regs->tef.sta & MCP251XFD_REG_TEFSTA_TEFFIF) ? "  chip-FIFO-full" :
		  !(regs->tef.sta & MCP251XFD_REG_TEFSTA_TEFNEIF) ? "  chip-FIFO-empty" : "") :
		 ("")),
		(mcp251xfd_get_ring_head(tef) == mcp251xfd_get_ring_tail(tef) &&
		 mcp251xfd_get_ring_tail(tef) == n ?
		 (priv->tef->head == priv->tef->tail ? "  priv-FIFO-empty" : "  priv-FIFO-full") :
		 ("")));
	pr_info("%16s = 0x%08x\n", "id", hw_tef_obj->id);
	pr_info("%16s = 0x%08x\n", "flags", hw_tef_obj->flags);
	pr_info("%16s = 0x%08x\n", "ts", hw_tef_obj->ts);
	__dump_mask(hw_tef_obj->flags, MCP251XFD_OBJ_FLAGS, SEQ, "0x%06lx", "Sequence");
	pr_info("\n");
}

static void
mcp251xfd_dump_ram_tef_obj(const struct mcp251xfd_priv *priv,
			   const struct mcp251xfd_dump_regs *regs,
			   const struct mcp251xfd_dump_ram *ram,
			   const struct mcp251xfd_ring *tef)
{
	int i;

	pr_info("\nTEF Overview:\n");
	pr_info("%16s =        0x%02x    0x%08x\n", "head (p)",
		mcp251xfd_get_ring_head(tef),
		tef->head);
	pr_info("%16s = 0x%02x   0x%02x    0x%08x\n", "tail (c/p)",
		mcp251xfd_dump_get_tef_tail(priv, regs),
		mcp251xfd_get_ring_tail(tef),
		tef->tail);
	pr_info("\n");

	for (i = 0; i < mcp251xfd_dump_get_tef_obj_num(priv, regs); i++) {
		const struct mcp251xfd_hw_tef_obj *hw_tef_obj;
		u16 hw_tef_obj_rel_addr;

		hw_tef_obj_rel_addr = mcp251xfd_dump_get_tef_obj_rel_addr(priv, i);

		hw_tef_obj = (const struct mcp251xfd_hw_tef_obj *)&ram->ram[hw_tef_obj_rel_addr];
		mcp251xfd_dump_ram_tef_obj_one(priv, regs, tef, hw_tef_obj, i);
	}
}

/* dump TX */

static void
mcp251xfd_dump_ram_tx_obj_one(const struct mcp251xfd_priv *priv,
			      const struct mcp251xfd_dump_regs *regs,
			      const struct mcp251xfd_ring *tx,
			      const struct mcp251xfd_hw_tx_obj_canfd *hw_tx_obj,
			      u8 n)
{
	pr_info("TX Object: 0x%02x (0x%03x)%s%s%s%s%s%s\n",
		n, mcp251xfd_dump_get_tx_obj_addr(priv, regs, n),
		mcp251xfd_dump_get_tx_head(priv, regs) == n ? "  chip-HEAD" : "",
		mcp251xfd_get_ring_head(tx) == n ? "  priv-HEAD" : "",
		mcp251xfd_dump_get_tx_tail(priv, regs) == n ? "  chip-TAIL" : "",
		mcp251xfd_get_ring_tail(tx) == n ? "  priv-TAIL" : "",
		mcp251xfd_dump_get_tx_tail(priv, regs) == n ?
		(!(regs->tx_fifo.sta & MCP251XFD_REG_FIFOSTA_TFNRFNIF) ? "  chip-FIFO-full" :
		 (regs->tx_fifo.sta & MCP251XFD_REG_FIFOSTA_TFERFFIF) ? "  chip-FIFO-empty" : "") :
		(""),
		(mcp251xfd_get_ring_head(tx) == mcp251xfd_get_ring_tail(tx) &&
		 mcp251xfd_get_ring_tail(tx) == n ?
		 (tx->head == tx->tail ? "  priv-FIFO-empty" : "  priv-FIFO-full") :
		 ("")));
	pr_info("%16s = 0x%08x\n", "id", hw_tx_obj->id);
	pr_info("%16s = 0x%08x\n", "flags", hw_tx_obj->flags);
	__dump_mask(hw_tx_obj->flags, MCP251XFD_OBJ_FLAGS, SEQ_MCP2517FD, "0x%06lx", "Sequence (MCP2517)");
	__dump_mask(hw_tx_obj->flags, MCP251XFD_OBJ_FLAGS, SEQ_MCP2518FD, "0x%06lx", "Sequence (MCP2518)");
	mcp251xfd_dump_ram_fifo_obj_data(priv,
					 hw_tx_obj->data,
					 FIELD_GET(MCP251XFD_OBJ_FLAGS_DLC, hw_tx_obj->flags));
	pr_info("\n");
}

static void
mcp251xfd_dump_ram_tx_obj(const struct mcp251xfd_priv *priv,
			  const struct mcp251xfd_dump_regs *regs,
			  const struct mcp251xfd_dump_ram *ram,
			  const struct mcp251xfd_ring *tx)
{
	int i;

	pr_info("\nTX Overview:\n");
	pr_info("%16s = 0x%02x    0x%02x    0x%08x\n", "head (c/p)",
		mcp251xfd_dump_get_tx_head(priv, regs),
		mcp251xfd_get_ring_head(tx),
		tx->head);
	pr_info("%16s = 0x%02x    0x%02x    0x%08x\n", "tail (c/p)",
		mcp251xfd_dump_get_tx_tail(priv, regs),
		mcp251xfd_get_ring_tail(tx),
		tx->tail);
	pr_info("\n");

	for (i = 0; i < mcp251xfd_dump_get_tx_obj_num(priv, regs); i++) {
		const struct mcp251xfd_hw_tx_obj_canfd *hw_tx_obj;
		u16 hw_tx_obj_rel_addr;

		hw_tx_obj_rel_addr = mcp251xfd_dump_get_tx_obj_rel_addr(priv, regs, i);

		hw_tx_obj = (const struct mcp251xfd_hw_tx_obj_canfd *)&ram->ram[hw_tx_obj_rel_addr];
		mcp251xfd_dump_ram_tx_obj_one(priv, regs, tx, hw_tx_obj, i);
	}
}

/* dump RX */

static void
mcp251xfd_dump_ram_rx_obj_one(const struct mcp251xfd_priv *priv,
			      const struct mcp251xfd_dump_regs *regs,
			      const struct mcp251xfd_ring *rx,
			      const struct mcp251xfd_hw_rx_obj_canfd *hw_rx_obj,
			      u8 n)
{
	pr_info("RX Object: 0x%02x (0x%03x)%s%s%s%s%s%s\n",
		n, mcp251xfd_dump_get_rx_obj_addr(priv, regs, n),
		mcp251xfd_dump_get_rx_head(priv, regs) == n ? "  chip-HEAD" : "",
		mcp251xfd_get_ring_head(rx) == n ? "  priv-HEAD" : "",
		mcp251xfd_dump_get_rx_tail(priv, regs) == n ? "  chip-TAIL" : "",
		mcp251xfd_get_ring_tail(rx) == n ? "  priv-TAIL" : "",
		mcp251xfd_dump_get_rx_tail(priv, regs) == n ?
		((regs->rx_fifo.sta & MCP251XFD_REG_FIFOSTA_TFERFFIF) ? "  chip-FIFO-full" :
		 !(regs->rx_fifo.sta & MCP251XFD_REG_FIFOSTA_TFNRFNIF) ? "  chip-FIFO-empty" : "") :
		(""),
		(mcp251xfd_get_ring_head(rx) == mcp251xfd_get_ring_tail(rx) &&
		 mcp251xfd_get_ring_tail(rx) == n ?
		 (priv->rx->head == priv->rx->tail ? "  priv-FIFO-empty" : "  priv-FIFO-full") :
		 ("")));
	pr_info("%16s = 0x%08x\n", "id", hw_rx_obj->id);
	pr_info("%16s = 0x%08x\n", "flags", hw_rx_obj->flags);
	pr_info("%16s = 0x%08x\n", "ts", hw_rx_obj->ts);
	mcp251xfd_dump_ram_fifo_obj_data(priv, hw_rx_obj->data, FIELD_GET(MCP251XFD_OBJ_FLAGS_DLC, hw_rx_obj->flags));
	pr_info("\n");
}

static void
mcp251xfd_dump_ram_rx_obj(const struct mcp251xfd_priv *priv,
			  const struct mcp251xfd_dump_regs *regs,
			  const struct mcp251xfd_dump_ram *ram,
			  const struct mcp251xfd_ring *rx)
{
	int i;

	pr_info("\nRX Overview:\n");
	pr_info("%16s = 0x%02x    0x%02x    0x%08x\n", "head (c/p)",
		mcp251xfd_dump_get_rx_head(priv, regs),
		mcp251xfd_get_ring_head(rx), rx->head);
	pr_info("%16s = 0x%02x    0x%02x    0x%08x\n", "tail (c/p)",
		mcp251xfd_dump_get_rx_tail(priv, regs),
		mcp251xfd_get_ring_tail(rx), rx->tail);
	pr_info("\n");

	for (i = 0; i < mcp251xfd_dump_get_rx_obj_num(priv, regs); i++) {
		const struct mcp251xfd_hw_rx_obj_canfd *hw_rx_obj;
		u16 hw_rx_obj_rel_addr;

		hw_rx_obj_rel_addr = mcp251xfd_dump_get_rx_obj_rel_addr(priv, regs, i);
		hw_rx_obj = (const struct mcp251xfd_hw_rx_obj_canfd *)&ram->ram[hw_rx_obj_rel_addr];

		mcp251xfd_dump_ram_rx_obj_one(priv, regs, rx, hw_rx_obj, i);
	}
}

#undef __dump_mask
#undef __dump_bit

static void mcp251xfd_dump_ram(const struct mcp251xfd_priv *priv, const struct mcp251xfd_dump_regs *regs, const struct mcp251xfd_dump_ram *ram)
{
	netdev_info(priv->ndev, "----------------------- RAM dump ----------------------\n");
	mcp251xfd_dump_ram_tef_obj(priv, regs, ram, priv->tef);
	mcp251xfd_dump_ram_tx_obj(priv, regs, ram, priv->tx);
	mcp251xfd_dump_ram_rx_obj(priv, regs, ram, priv->rx);
	netdev_info(priv->ndev, "------------------------- end -------------------------\n");
}

void mcp251xfd_dump(struct mcp251xfd_priv *priv)
{
	struct mcp251xfd_dump_regs regs;
	struct mcp251xfd_dump_ram ram;
	struct mcp251xfd_dump_regs_mcp251xfd regs_mcp251xfd;
	int err;

	BUILD_BUG_ON(sizeof(struct mcp251xfd_dump_regs) !=
		     MCP251XFD_REG_FIFOUA(31) - MCP251XFD_REG_CON + 4);

	err = regmap_bulk_read(priv->map, MCP251XFD_REG_CON,
			       &regs, sizeof(regs) / sizeof(u32));
	if (err)
		return;

	err = regmap_bulk_read(priv->map, MCP251XFD_RAM_START,
			       &ram, sizeof(ram) / sizeof(u32));
	if (err)
		return;

	err = regmap_bulk_read(priv->map, MCP251XFD_REG_OSC,
			       &regs_mcp251xfd, sizeof(regs_mcp251xfd) / sizeof(u32));
	if (err)
		return;

	mcp251xfd_dump_regs(priv, &regs, &regs_mcp251xfd);
	mcp251xfd_dump_ram(priv, &regs, &ram);
}
