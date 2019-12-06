# Kickstart guide to can-j1939 on linux

## Prepare using VCAN

You may skip this step entirely if you have a functional
**can0** bus on your system.

Load module, when *vcan* is not in-kernel

	modprobe vcan

Create a virtual can0 device and start the device

	ip link add can0 type vcan
	ip link set can0 up

## First steps with j1939

Use [testj1939](testj1939.c)

When *can-j1939* is compiled as module, opening a socket will load it,
__or__ you can load it manually

	modprobe can-j1939

Most of the subsequent examples will use 2 sockets programs (in 2 terminals).
One will use CAN_J1939 sockets using *testj1939*,
and the other will use CAN_RAW sockets using cansend+candump.

testj1939 can be told to print the used API calls by adding **-v** program argument.

### receive without source address

Do in terminal 1

	testj1939 -B -r can0

Send raw CAN in terminal 2

	cansend can0 1823ff40#0123

You should have this output in terminal 1

	40 02300: 01 23

This means, from NAME 0, SA 40, PGN 02300 was received,
with 2 databytes, *01* & *02*.

now emit this CAN message:

	cansend can0 18234140#0123

In J1939, this means that ECU 0x40 sends directly to ECU 0x41
Since we did not bind to address 0x41, this traffic
is not meant for us and *testj1939* does not receive it.

### receive with source address

Terminal 1:

	testj1939 -r can0:0x80

Terminal 2:

	cansend can0 18238040#0123

Will emit this output

	40 02300: 01 23

This is because the traffic had destination address __0x80__ .

### send

Open in terminal 1:

	candump -L can0

And to these test in another terminal

	testj1939 -B -s can0:0x80 can0:,0x3ffff

This produces **1BFFFF80#0123456789ABCDEF** on CAN.

Note: To be able to send a broadcast we need to use, we need to use "-B" flag.

### Multiple source addresses on 1 CAN device

	testj1939 -B -s can0:0x90 can0:,0x3ffff

produces **1BFFFF90#0123456789ABCDEF** ,

### Use PDU1 PGN

	testj1939 -B -s can0:0x80 can0:,0x12300

emits **1923FF80#0123456789ABCDEF** .

Note that the PGN is **0x12300**, and destination address is **0xff**.

### Use destination address info

Since in this example we use unicast source and destination addresses, we do
not need to use "-B" (broadcast) flag.

The destination field may be set during sendto().
*testj1939* implements that like this

	testj1939 -s can0:0x80 can0:0x40,0x12300

emits **19234080#0123456789ABCDEF** .

The destination CAN iface __must__ always match the source CAN iface.
Specifying one during bind is therefore sufficient.

	testj1939 -s can0:0x80 :0x40,0x12300

emits the very same.

### Emit different PGNs using the same socket

The PGN is provided in both __bind( *sockname* )__ and
__sendto( *peername* )__ , and only one is used.
*peername* PGN has highest precedence.

For broadcasted transmissions

	testj1939 -B -s can0:0x80 :,0x32100

emits **1B21FF80#0123456789ABCDEF**

Destination specific transmissions

	testj1939 -s can0:0x80,0x12300 :0x40,0x32100

emits **1B214080#0123456789ABCDEF** .

It makes sometimes sense to omit the PGN in __bind( *sockname* )__ .

### Larger packets

J1939 transparently switches to *Transport Protocol* when packets
do not fit into single CAN packets.

	testj1939 -B -s20 can0:0x80 :,0x12300

emits:

	18ECFF80#20140003FF002301
	18EBFF80#010123456789ABCD
	18EBFF80#02EF0123456789AB
	18EBFF80#03CDEF01234567FF

The fragments for broadcasted *Transport Protocol* are separated
__50ms__ from each other.
Destination specific *Transport Protocol* applies flow control
and may emit CAN packets much faster.

First assign 0x90 to the local system.
This becomes important because the kernel must interact in the
transport protocol sessions before the complete packet is delivered.

	testj1939 can0:0x90 -r &

Now test:

	testj1939 -s20 can0:0x80 :0x90,0x12300

emits:

	18EC9080#1014000303002301
	18EC8090#110301FFFF002301
	18EB9080#010123456789ABCD
	18EB9080#02EF0123456789AB
	18EB9080#03CDEF01234567FF
	18EC8090#13140003FF002301

The flow control causes a bit overhead.
This overhead scales very good for larger J1939 packets.

## Advanced topics with j1939

### Change priority of J1939 packets

	testj1939 -B -s can0:0x80 :,0x0100
	testj1939 -B -s -p3 can0:0x80 :,0x0200

emits

	1801FF80#0123456789ABCDEF
	0C02FF80#0123456789ABCDEF

### using connect

### advanced filtering

## dynamic addressing
