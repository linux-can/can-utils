////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                                //
//  canerrsim - utility to simulate SocketCAN error messages, by Zeljko Avramovic (c) 2024        //
//                                                                                                //
//  SPDX-License-Identifier: LGPL-2.1-or-later OR BSD-3-Clause                                    //
//                                                                                                //
//  Virtual CAN adapter vcan0 is hard coded and you can bring it up like this:                    //
//  sudo modprobe vcan                                                                            //
//  sudo ip link add dev vcan0 type vcan                                                          //
//  sudo ip link set vcan0 mtu 72              # needed for CAN FD                                //
//  sudo ip link set vcan0 up                                                                     //
//                                                                                                //
//  To simulate error messages use canerrsim utility like this:                                   //
//  ./canerrsim vcan0 LostArBit=09 Data4=AA TX BusOff NoAck ShowBits                              //
//                                                                                                //
//  That should show in canerrdump utility as:                                                    //
//  0x06A [8] 09 00 80 00 AA 00 00 00  ERR=LostArBit09,NoAck,BusOff,Prot(Type(TX),Loc(Unspec))    //
//                                                                                                //
//  Alternatively, you could use candump from can-utils to check only error messages like this:   //
//  candump -tA -e -c -a any,0~0,#FFFFFFFF                                                        //
//                                                                                                //
////////////////////////////////////////////////////////////////////////////////////////////////////

#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define STR_EQUAL 0

void show_help_and_exit()
{
	printf("\n");
	printf("Usage: canerrsim <CAN interface> <options>\n");
	printf("\n");
	printf("CAN interface:          ( CAN interface is case sensitive )\n");
	printf("    can0                ( or can1, can2 or virtual ones like vcan0, vcan1...\n");
	printf("\n");
	printf("Options:                ( options are not case sensitive )\n");
	printf("                        ( ERROR CLASS (MASK) IN CAN ID: )\n");
	printf("    TxTimeout           ( TX timeout by netdevice driver )\n");
	printf("    NoAck               ( received no ACK on transmission )\n");
	printf("    BusOff              ( bus off )\n");
	printf("    BusError            ( bus error, may flood! )\n");
	printf("    Restarted           ( controller restarted )\n");
	printf("    TxCount=<00..FF>    ( TX error counter )\n");
	printf("    RxCount=<00..FF>    ( RX error counter )\n");
	printf("                        ( ARBITRATIONLOST IN CAN ID + BIT NUMBER IN DATA[0]: )\n");
	printf("    LostArBit=<00..29>  ( decimal lost arbitration bit number in bitstream )\n");
	printf("                        ( CONTROLLER IN CAN ID + ERROR STATUS IN DATA[1]: )\n");
	printf("    OverflowRX          ( RX buffer overflow )\n");
	printf("    OverflowTX          ( TX buffer overflow )\n");
	printf("    WarningRX           ( reached warning level for RX errors )\n");
	printf("    WarningTX           ( reached warning level for TX errors )\n");
	printf("    PassiveRX           ( reached error passive status RX, errors > 127 )\n");
	printf("    PassiveTX           ( reached error passive status TX, errors > 127 )\n");
	printf("    Active              ( recovered to error active state )\n");
	printf("                        ( PROTOCOL ERROR IN CAN ID + TYPE IN DATA[2]: )\n");
	printf("    SingleBit           ( single bit error )\n");
	printf("    FrameFormat         ( frame format error )\n");
	printf("    BitStuffing         ( bit stuffing error )\n");
	printf("    Bit0                ( unable to send dominant bit )\n");
	printf("    Bit1                ( unable to send recessive bit )\n");
	printf("    BusOverload         ( bus overload )\n");
	printf("    ActiveAnnouncement  ( active error announcement )\n");
	printf("    TX                  ( error occurred on transmission )\n");
	printf("                        ( PROTOCOL ERROR IN CAN ID + LOCATION IN DATA[3]: )\n");
	printf("    SOF                 ( start of frame )\n");
	printf("    ID28_21             ( ID bits 21..28, SFF: 3..10 )\n");
	printf("    ID20_18             ( ID bits 18..20, SFF: 0..2 )\n");
	printf("    SRTR                ( substitute RTR, SFF: RTR )\n");
	printf("    IDE                 ( identifier extension )\n");
	printf("    ID17_13             ( ID bits 13..17 )\n");
	printf("    ID12_05             ( ID bits 5..12 )\n");
	printf("    ID04_00             ( ID bits 0..4 )\n");
	printf("    RTR                 ( RTR )\n");
	printf("    RES1                ( reserved bit 1 )\n");
	printf("    RES0                ( reserved bit 0 )\n");
	printf("    DLC                 ( data length code )\n");
	printf("    DATA                ( data section )\n");
	printf("    CRC_SEQ             ( CRC sequence )\n");
	printf("    CRC_DEL             ( CRC delimiter )\n");
	printf("    ACK                 ( ACK slot )\n");
	printf("    ACK_DEL             ( ACK delimiter )\n");
	printf("    EOF                 ( end of frame )\n");
	printf("    INTERM              ( intermission )\n");
	printf("                        ( TRANSCEIVER ERROR IN CAN ID + STATUS IN DATA[4]: )\n");
	printf("                        ( CANH CANL )\n");
	printf("    TransUnspec         ( 0000 0000 )\n");
	printf("    CanHiNoWire         ( 0000 0100 )\n");
	printf("    CanHiShortToBAT     ( 0000 0101 )\n");
	printf("    CanHiShortToVCC     ( 0000 0110 )\n");
	printf("    CanHiShortToGND     ( 0000 0111 )\n");
	printf("    CanLoNoWire         ( 0100 0000 )\n");
	printf("    CanLoShortToBAT     ( 0101 0000 )\n");
	printf("    CanLoShortToVCC     ( 0110 0000 )\n");
	printf("    CanLoShortToGND     ( 0111 0000 )\n");
	printf("    CanLoShortToCanHi   ( 1000 0000 )\n");
	printf("                        ( CUSTOM BYTE TO DATA[0..7]: )\n");
	printf("    Data<0..7>=<00..FF> ( write hex number to one of 8 payload bytes )\n");
	printf("                        ( DEBUG HELPERS: )\n");
	printf("    ShowBits            ( display all frame bits )\n");
	printf("\n");
	printf("Examples:\n");
	printf("\n");
	printf("    ./canerrsim can1 LostArBit=09 Data3=AA Data4=BB ShowBits\n");
	printf("    ( can1: 9th arb. bit lost, custom bytes in Data[3] and Data[4], show debug frame bits )\n");
	printf("\n");
	printf("    ./canerrsim vcan0 NoAck TxTimeout Active\n");
	printf("    ( vcan0: received no ACK on transmission, driver timeout, protocol type active error announcement )\n");
	printf("\n");
	printf("    ./canerrsim vcan0 BusError CanHiNoWire Restarted INTERM\n");
	printf("    ( vcan0: bus error, lost CANH wiring, controller restarted, protocol location intermission )\n");
	printf("\n");
	exit(EXIT_SUCCESS);
}

void err_exit(const char *msg)
{
	printf("%s", msg);
	exit(EXIT_FAILURE);
}

void show_custom_format_and_exit(const char *param, const char *format)
{
	char str_buf[80];
	sprintf(str_buf, format, param);
	err_exit(str_buf);
}

void show_invalid_option(const char *option)
{
	show_custom_format_and_exit(option, "Error: Invalid option %s\n");
}

void show_err_and_exit(const char *err_type)
{
	show_custom_format_and_exit(err_type, "Error: You can only have one %s parameter!\n");
}

void show_loc_err_and_exit()
{
	show_err_and_exit("protocol location");
}

void show_arb_err_and_exit()
{
	show_err_and_exit("arbitration bit");
}

void show_transc_err_and_exit()
{
	show_err_and_exit("transceiver");
}

void print_binary(uint32_t number)
{
	uint32_t mask = 0x80000000; // start with the most significant bit
	for (int i = 0; i < 32; i++) {
		putchar((number & mask) ? '1' : '0');
		mask >>= 1; // shift the mask to the right
	}
}

int main(int argc, char *argv[])
{
	int sock;
	struct sockaddr_can addr;
	struct ifreq ifr;
	struct can_frame frame;
	bool show_bits = false, location_processed = false, transceiver_processed = false, arbitration_processed = false;
	char tmp_str[256];

	printf("CAN Sockets Error Messages Simulator\n");
	if (argc < 3)
		show_help_and_exit();

	// initialize CAN frame
	memset(&frame, 0, sizeof(frame));
	frame.can_id = CAN_ERR_FLAG;
	frame.can_dlc = CAN_ERR_DLC;

	// Parse command line parameters
	for (int i = 2; i < argc; i++) {
		//printf("strlen(argv[%d]) = %d\n", i, strlen(argv[i]));

		// error class (mask) in can_id
		if (strcasecmp(argv[i], "TxTimeout") == STR_EQUAL)
			frame.can_id |= CAN_ERR_TX_TIMEOUT; // generate TxTimeout error
		else if (strcasecmp(argv[i], "NoAck") == STR_EQUAL)
			frame.can_id |= CAN_ERR_ACK; // generate NoAck error
		else if (strcasecmp(argv[i], "BusOff") == STR_EQUAL)
			frame.can_id |= CAN_ERR_BUSOFF; // generate BusOff error
		else if (strcasecmp(argv[i], "BusError") == STR_EQUAL)
			frame.can_id |= CAN_ERR_BUSERROR; // generate BusError error
		else if (strcasecmp(argv[i], "Restarted") == STR_EQUAL)
			frame.can_id |= CAN_ERR_RESTARTED; // generate Restarted error
		// error status of CAN controller / data[1]
		else if (strcasecmp(argv[i], "OverflowRX") == STR_EQUAL) {
			frame.can_id |= CAN_ERR_CRTL; // generate Controller error
			frame.data[1] |= CAN_ERR_CRTL_RX_OVERFLOW; // generate RX Overflow suberror
		} else if (strcasecmp(argv[i], "OverflowTX") == STR_EQUAL) {
			frame.can_id |= CAN_ERR_CRTL; // generate Controller error
			frame.data[1] |= CAN_ERR_CRTL_TX_OVERFLOW; // generate TX Overflow suberror
		} else if (strcasecmp(argv[i], "WarningRX") == STR_EQUAL) {
			frame.can_id |= CAN_ERR_CRTL; // generate Controller error
			frame.data[1] |= CAN_ERR_CRTL_RX_WARNING; // generate RX Warning suberror
		} else if (strcasecmp(argv[i], "WarningTX") == STR_EQUAL) {
			frame.can_id |= CAN_ERR_CRTL; // generate Controller error
			frame.data[1] |= CAN_ERR_CRTL_TX_WARNING; // generate TX Warning suberror
		} else if (strcasecmp(argv[i], "PassiveRX") == STR_EQUAL) {
			frame.can_id |= CAN_ERR_CRTL; // generate Controller error
			frame.data[1] |= CAN_ERR_CRTL_RX_PASSIVE; // generate RX Passive suberror
		} else if (strcasecmp(argv[i], "PassiveTX") == STR_EQUAL) {
			frame.can_id |= CAN_ERR_CRTL; // generate Controller error
			frame.data[1] |= CAN_ERR_CRTL_TX_PASSIVE; // generate TX Passive suberror
		} else if (strcasecmp(argv[i], "Active") == STR_EQUAL) {
			frame.can_id |= CAN_ERR_CRTL; // generate Controller error
			frame.data[1] |= CAN_ERR_CRTL_ACTIVE; // generate Active suberror
		} else if (strcasecmp(argv[i], "CtrlUnspec") == STR_EQUAL) {
			frame.can_id |= CAN_ERR_CRTL; // generate Controller error
			frame.data[1] = CAN_ERR_CRTL_UNSPEC; // generate Unspec suberror
		}
		// error in CAN protocol (type) / data[2]
		else if (strcasecmp(argv[i], "SingleBit") == STR_EQUAL) {
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Type error
			frame.data[2] = CAN_ERR_PROT_BIT; // generate SingleBit suberror
		} else if (strcasecmp(argv[i], "FrameFormat") == STR_EQUAL) {
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Type error
			frame.data[2] = CAN_ERR_PROT_FORM; // generate FrameFormat suberror
		} else if (strcasecmp(argv[i], "BitStuffing") == STR_EQUAL) {
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Type error
			frame.data[2] = CAN_ERR_PROT_STUFF; // generate BitStuffing suberror
		} else if (strcasecmp(argv[i], "Bit0") == STR_EQUAL) {
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Type error
			frame.data[2] = CAN_ERR_PROT_BIT0; // generate Bit0 suberror
		} else if (strcasecmp(argv[i], "Bit1") == STR_EQUAL) {
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Type error
			frame.data[2] = CAN_ERR_PROT_BIT1; // generate Bit1 suberror
		} else if (strcasecmp(argv[i], "BusOverload") == STR_EQUAL) {
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Type error
			frame.data[2] = CAN_ERR_PROT_OVERLOAD; // generate BusOverload suberror
		} else if (strcasecmp(argv[i], "ActiveAnnouncement") == STR_EQUAL) {
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Type error
			frame.data[2] = CAN_ERR_PROT_ACTIVE; // generate ActiveAnnouncement suberror
		} else if (strcasecmp(argv[i], "TX") == STR_EQUAL) {
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Type error
			frame.data[2] = CAN_ERR_PROT_TX; // generate TX suberror
		} else if (strcasecmp(argv[i], "ProtUnspec") == STR_EQUAL) {
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Type error
			frame.data[2] = CAN_ERR_PROT_UNSPEC; // generate Unspec suberror
		}
		// error in CAN protocol (location) / data[3]
		else if (strcasecmp(argv[i], "LocUnspec") == STR_EQUAL) {
			if (location_processed)
				show_loc_err_and_exit();
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Location error
			frame.data[3] = CAN_ERR_PROT_LOC_UNSPEC; // generate Unspec suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "SOF") == STR_EQUAL) {
			if (location_processed)
				show_loc_err_and_exit();
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Location error
			frame.data[3] = CAN_ERR_PROT_LOC_SOF; // generate SOF suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "SOF") == STR_EQUAL) {
			if (location_processed)
				show_loc_err_and_exit();
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Location error
			frame.data[3] = CAN_ERR_PROT_LOC_SOF; // generate SOF suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "ID28_21") == STR_EQUAL) {
			if (location_processed)
				show_loc_err_and_exit();
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Location error
			frame.data[3] = CAN_ERR_PROT_LOC_ID28_21; // generate ID28_21 suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "ID20_18") == STR_EQUAL) {
			if (location_processed)
				show_loc_err_and_exit();
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Location error
			frame.data[3] = CAN_ERR_PROT_LOC_ID20_18; // generate ID20_18 suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "SRTR") == STR_EQUAL) {
			if (location_processed)
				show_loc_err_and_exit();
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Location error
			frame.data[3] = CAN_ERR_PROT_LOC_SRTR; // generate SRTR suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "IDE") == STR_EQUAL) {
			if (location_processed)
				show_loc_err_and_exit();
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Location error
			frame.data[3] = CAN_ERR_PROT_LOC_IDE; // generate IDE suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "ID17_13") == STR_EQUAL) {
			if (location_processed)
				show_loc_err_and_exit();
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Location error
			frame.data[3] = CAN_ERR_PROT_LOC_ID17_13; // generate ID17_13 suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "ID12_05") == STR_EQUAL) {
			if (location_processed)
				show_loc_err_and_exit();
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Location error
			frame.data[3] = CAN_ERR_PROT_LOC_ID12_05; // generate ID12_05 suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "ID04_00") == STR_EQUAL) {
			if (location_processed)
				show_loc_err_and_exit();
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Location error
			frame.data[3] = CAN_ERR_PROT_LOC_ID04_00; // generate ID04_00 suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "RTR") == STR_EQUAL) {
			if (location_processed)
				show_loc_err_and_exit();
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Location error
			frame.data[3] = CAN_ERR_PROT_LOC_RTR; // generate RTR suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "RES1") == STR_EQUAL) {
			if (location_processed)
				show_loc_err_and_exit();
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Location error
			frame.data[3] = CAN_ERR_PROT_LOC_RES1; // generate RES1 suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "RES0") == STR_EQUAL) {
			if (location_processed)
				show_loc_err_and_exit();
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Location error
			frame.data[3] = CAN_ERR_PROT_LOC_RES0; // generate RES0 suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "DLC") == STR_EQUAL) {
			if (location_processed)
				show_loc_err_and_exit();
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Location error
			frame.data[3] = CAN_ERR_PROT_LOC_DLC; // generate DLC suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "DATA") == STR_EQUAL) {
			if (location_processed)
				show_loc_err_and_exit();
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Location error
			frame.data[3] = CAN_ERR_PROT_LOC_DATA; // generate DATA suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "CRC_SEQ") == STR_EQUAL) {
			if (location_processed)
				show_loc_err_and_exit();
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Location error
			frame.data[3] = CAN_ERR_PROT_LOC_CRC_SEQ; // generate CRC_SEQ suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "CRC_DEL") == STR_EQUAL) {
			if (location_processed)
				show_loc_err_and_exit();
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Location error
			frame.data[3] = CAN_ERR_PROT_LOC_CRC_DEL; // generate CRC_DEL suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "ACK") == STR_EQUAL) {
			if (location_processed)
				show_loc_err_and_exit();
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Location error
			frame.data[3] = CAN_ERR_PROT_LOC_ACK; // generate ACK suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "ACK_DEL") == STR_EQUAL) {
			if (location_processed)
				show_loc_err_and_exit();
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Location error
			frame.data[3] = CAN_ERR_PROT_LOC_ACK_DEL; // generate ACK_DEL suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "EOF") == STR_EQUAL) {
			if (location_processed)
				show_loc_err_and_exit();
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Location error
			frame.data[3] = CAN_ERR_PROT_LOC_EOF; // generate EOF suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "INTERM") == STR_EQUAL) {
			if (location_processed)
				show_loc_err_and_exit();
			frame.can_id |= CAN_ERR_PROT; // generate Protocol Location error
			frame.data[3] = CAN_ERR_PROT_LOC_INTERM; // generate INTERM suberror
			location_processed = true;
		}
		// error status of CAN transceiver / data[4]
		else if (strcasecmp(argv[i], "TransUnspec") == STR_EQUAL) {
			if (transceiver_processed)
				show_transc_err_and_exit();
			frame.can_id |= CAN_ERR_TRX; // generate Transceiver error
			frame.data[4] = CAN_ERR_TRX_UNSPEC; // generate EOF suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "CanHiNoWire") == STR_EQUAL) {
			if (transceiver_processed)
				show_transc_err_and_exit();
			frame.can_id |= CAN_ERR_TRX; // generate Transceiver error
			frame.data[4] = CAN_ERR_TRX_CANH_NO_WIRE; // generate CanHiNoWire suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "CanHiShortToBAT") == STR_EQUAL) {
			if (transceiver_processed)
				show_transc_err_and_exit();
			frame.can_id |= CAN_ERR_TRX; // generate Transceiver error
			frame.data[4] = CAN_ERR_TRX_CANH_SHORT_TO_BAT; // generate CanHiShortToBAT suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "CanHiShortToVCC") == STR_EQUAL) {
			if (transceiver_processed)
				show_transc_err_and_exit();
			frame.can_id |= CAN_ERR_TRX; // generate Transceiver error
			frame.data[4] = CAN_ERR_TRX_CANH_SHORT_TO_VCC; // generate CanHiShortToVCC suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "CanHiShortToGND") == STR_EQUAL) {
			if (transceiver_processed)
				show_transc_err_and_exit();
			frame.can_id |= CAN_ERR_TRX; // generate Transceiver error
			frame.data[4] = CAN_ERR_TRX_CANH_SHORT_TO_GND; // generate CanHiShortToGND suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "CanLoNoWire") == STR_EQUAL) {
			if (transceiver_processed)
				show_transc_err_and_exit();
			frame.can_id |= CAN_ERR_TRX; // generate Transceiver error
			frame.data[4] = CAN_ERR_TRX_CANL_NO_WIRE; // generate CanLoNoWire suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "CanLoShortToBAT") == STR_EQUAL) {
			if (transceiver_processed)
				show_transc_err_and_exit();
			frame.can_id |= CAN_ERR_TRX; // generate Transceiver error
			frame.data[4] = CAN_ERR_TRX_CANL_SHORT_TO_BAT; // generate CanLoShortToBAT suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "CanLoShortToVCC") == STR_EQUAL) {
			if (transceiver_processed)
				show_transc_err_and_exit();
			frame.can_id |= CAN_ERR_TRX; // generate Transceiver error
			frame.data[4] = CAN_ERR_TRX_CANL_SHORT_TO_VCC; // generate CanLoShortToVCC suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "CanLoShortToGND") == STR_EQUAL) {
			if (transceiver_processed)
				show_transc_err_and_exit();
			frame.can_id |= CAN_ERR_TRX; // generate Transceiver error
			frame.data[4] = CAN_ERR_TRX_CANL_SHORT_TO_GND; // generate CanLoShortToGND suberror
			location_processed = true;
		} else if (strcasecmp(argv[i], "CanLoShortToCanHi") == STR_EQUAL) {
			if (transceiver_processed)
				show_transc_err_and_exit();
			frame.can_id |= CAN_ERR_TRX; // generate Transceiver error
			frame.data[4] = CAN_ERR_TRX_CANL_SHORT_TO_CANH; // generate CanLoShortToCanHi suberror
			location_processed = true;
		}
		// LostArBit=29 (Totallength=12)
		else if ((strlen(argv[i]) == 12) && // 'LostArBit=29'
			 (argv[i][9] == '=') && // '='
			 (argv[i][10] >= '0' && argv[i][10] <= '2') && // valid bits are from 00 to 29 (in decimal)
			 (argv[i][11] >= '0' && argv[i][11] <= '9')) { // valid bits are from 00 to 29 (in decimal)
			unsigned char arb_bit_num = (argv[i][10] - '0') * 10 + argv[i][11] - '0'; // convert decimal bitnumber to byte
			argv[i][9] = 0; // terminate string for comparison
			if (strcasecmp(argv[i], "LostArBit") == STR_EQUAL) {
				if (arbitration_processed)
					show_arb_err_and_exit();
				frame.can_id |= CAN_ERR_LOSTARB; // generate LostArbitartionBit error
				frame.data[0] = arb_bit_num; // bitnumber
				arbitration_processed = true;
			} else {
				argv[i][9] = '='; // undo string termination
				show_invalid_option(argv[i]);
			}
		}
		// Data1=F4 (Totallength=8)                            // since this does not set any error bit, has to be combined with other errors
		else if ((strlen(argv[i]) == 8) && // 'Data1=F4'
			 (argv[i][4] >= '0' && argv[i][4] <= '7') && // valid data bytes are from 0 to 7 (in decimal)
			 (argv[i][5] == '=') && // '='
			 ((argv[i][6] >= '0' && argv[i][6] <= '9') || (argv[i][6] >= 'A' && argv[i][6] <= 'F')) && // first hexadecimal digit
			 ((argv[i][7] >= '0' && argv[i][7] <= '9') || (argv[i][6] >= 'A' && argv[i][6] <= 'F'))) { // second hexadecimal digit
			unsigned char data_byte_value, data_byte_no = 0;
			data_byte_no = argv[i][4] - '0'; // convert order number of data byte (Data1 to 1, Data2 to 2...)
			data_byte_value = 0;
			if (argv[i][6] >= 'A') // convert higher digit hexadecimal char to byte
				data_byte_value += (argv[i][6] - 'A' + 10) * 16;
			else
				data_byte_value += (argv[i][6] - '0') * 16;
			if (argv[i][7] >= 'A') // convert lower digit hexadecimal char to byte
				data_byte_value += (argv[i][7] - 'A' + 10);
			else
				data_byte_value += (argv[i][7] - '0');
			argv[i][4] = 0; // terminate string for comparison
			if (strcasecmp(argv[i], "Data") == STR_EQUAL) {
				if (transceiver_processed)
					show_transc_err_and_exit();
				frame.data[data_byte_no] = data_byte_value; // populate proper data byte
				arbitration_processed = true;
			} else {
				argv[i][4] = data_byte_no + '0'; // undo string termination
				show_invalid_option(argv[i]);
			}
		}
		// RxCount=F4 or TxCount=3A (Totallength=10)
		else if ((strlen(argv[i]) == 10) && // 'RxCounter=F4' or 'TxCounter=3A'
			 (argv[i][7] == '=') && // '='
			 ((argv[i][8] >= '0' && argv[i][8] <= '9') || (argv[i][8] >= 'A' && argv[i][8] <= 'F')) && // first hexadecimal digit
			 ((argv[i][9] >= '0' && argv[i][9] <= '9') || (argv[i][9] >= 'A' && argv[i][9] <= 'F'))) { // second hexadecimal digit
			unsigned char counter_value = 0;
			if (argv[i][8] >= 'A') // convert higher digit hexadecimal char to byte
				counter_value += (argv[i][8] - 'A' + 10) * 16;
			else
				counter_value += (argv[i][8] - '0') * 16;
			if (argv[i][9] >= 'A') // convert lower digit hexadecimal char to byte
				counter_value += (argv[i][9] - 'A' + 10);
			else
				counter_value += (argv[i][9] - '0');
			argv[i][7] = 0; // terminate string for comparison
			if (strcasecmp(argv[i], "TxCount") == STR_EQUAL) {
				frame.can_id |= CAN_ERR_CNT; // generate TxCounter error
				frame.data[6] = counter_value; // populate proper data byte
			} else if (strcasecmp(argv[i], "RxCount") == STR_EQUAL) {
				frame.can_id |= CAN_ERR_CNT; // generate RxCounter error
				frame.data[7] = counter_value; // populate proper data byte
			} else {
				argv[i][7] = '='; // undo string termination
				show_invalid_option(argv[i]);
			}
		} else if (strcasecmp(argv[i], "ShowBits") == STR_EQUAL) // DEBUG helper
			show_bits = true; // Display frame as bits
		else
			show_invalid_option(argv[i]);
	}

	if (show_bits == true) {
		printf("CAN ID   = ");
		print_binary(frame.can_id);
		printf("\n");
		// printf("frame.can_dlc = %d\n", frame.can_dlc);
		printf("CAN Data = ");
		for (size_t i = 0; i < frame.can_dlc; i++)
			printf("%02X ", frame.data[i]);
		printf("\n");
	}

	// create socket
	if ((sock = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0)
		err_exit("Error while opening socket");

	// set interface name
	strcpy(ifr.ifr_name, argv[1]); // can0, vcan0...
	if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
		sprintf(tmp_str, "Error setting CAN interface name %s", argv[1]);
		err_exit(tmp_str);
	}

	// bind socket to the CAN interface
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		err_exit("Error in socket bind");

	// Send CAN error frame
	if (write(sock, &frame, sizeof(frame)) < 0)
		err_exit("Error writing to socket");
	else
		printf("CAN error frame sent\n");

	close(sock);

	return 0;
}
