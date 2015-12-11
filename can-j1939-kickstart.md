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

When *can-j1939* is compiled as module, load it.

	modprobe can-j1939

Enable the j1939 protocol stack on the CAN device

	ip link set can0 j1939 on

Most of the subsequent examples will use 2 sockets programs (in 2 terminals).
One will use CAN_J1939 sockets using *testj1939*,
and the other will use CAN_RAW sockets using cansend+candump.

testj1939 can be told to print the used API calls by adding **-v** program argument.

### receive without source address

Do in terminal 1

	./testj1939 -r can0:

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

### Use source address

	./testj1939 can0:0x80

will say

	./testj1939: bind(): Cannot assign requested address

Since J1939 maintains addressing, **0x80** has not yet been assigned
as an address on **can0** . This behaviour is very similar to IP
addressing: you cannot bind to an address that is not your own.

Now tell the kernel that we *own* address 0x80.
It will be available from now on.

	ip addr add j1939 0x80 dev can0
	./testj1939 can0:0x80

now succeeds.

### receive with source address

Terminal 1:

	./testj1939 -r can0:0x80

Terminal 2:

	cansend can0 18238040#0123

Will emit this output

	40 02300: 01 23

This is because the traffic had destination address __0x80__ .

### send

Open in terminal 1:

	candump -L can0

And to these test in another terminal

	./testj1939 -s can0:0x80

This produces **1BFFFF80#0123456789ABCDEF** on CAN.

	./testj1939 -s can0:

will produce exactly the same because **0x80** is the only
address currently assigned to **can0:** and is used by default.

### Multiple source addresses on 1 CAN device

	ip addr add j1939 0x90 dev can0

	./testj1939 -s can0:0x90

produces **1BFFFF90#0123456789ABCDEF** ,

	./testj1939 -s can0:

still produces **1BFFFF80#0123456789ABCDEF** , since **0x80**
is the default _source address_.
Check

	ip addr show can0

emits

	X: can0: <NOARP,UP,LOWER_UP> mtu 16 qdisc noqueue state UNKNOWN 
	    link/can 
	    can-j1939 0x80 scope link 
	    can-j1939 0x90 scope link

0x80 is the first address on can0.

### Use specific PGN

	./testj1939 -s can0:,0x12345

emits **1923FF80#0123456789ABCDEF** .

Note that the real PGN is **0x12300**, and destination address is **0xff**.

### Emit destination specific packets

The destination field may be set during sendto().
*testj1939* implements that like this

	./testj1939 -s can0:,0x12345 can0:0x40

emits **19234080#0123456789ABCDEF** .

The destination CAN iface __must__ always match the source CAN iface.
Specifing one during bind is therefore sufficient.

	./testj1939 -s can0:,0x12300 :0x40

emits the very same.

### Emit different PGNs using the same socket

The PGN is provided in both __bind( *sockname* )__ and
__sendto( *peername* )__ , and only one is used.
*peername* PGN has highest precedence.

For broadcasted transmissions

	./testj1939 -s can0:,0x12300 :,0x32100

emits **1B21FF80#0123456789ABCDEF** rather than 1923FF80#012345678ABCDEF

Desitination specific transmissions

	./testj1939 -s can0:,0x12300 :0x40,0x32100

emits **1B214080#0123456789ABCDEF** .

It makes sometimes sense to omit the PGN in __bind( *sockname* )__ .

### Larger packets

J1939 transparently switches to *Transport Protocol* when packets
do not fit into single CAN packets.

	./testj1939 -s20 can0:0x80 :,0x12300

emits:

	18ECFF80#20140003FF002301
	18EBFF80#010123456789ABCD
	18EBFF80#02EF0123456789AB
	18EBFF80#03CDEF01234567

The fragments for broadcasted *Transport Protocol* are seperated
__50ms__ from each other.  
Destination specific *Transport Protocol* applies flow control
and may emit CAN packets much faster.

	./testj1939 -s20 can0:0x80 :0x90,0x12300

emits:

	18EC9080#1014000303002301
	18EC8090#110301FFFF002301
	18EB9080#010123456789ABCD
	18EB9080#02EF0123456789AB
	18EB9080#03CDEF01234567
	18EC8090#13140003FF002301

The flow control causes a bit overhead.
This overhead scales very good for larger J1939 packets.

## Advanced topics with j1939

### Change priority of J1939 packets

	./testj1939 -s can0:0x80,0x0100
	./testj1939 -s -p3 can0:0x80,0x0200

emits
	
	1801FF80#0123456789ABCDEF	
	0C02FF80#0123456789ABCDEF

### using connect

### advanced filtering

## dynamic addressing

