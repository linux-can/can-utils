<p align="center">
<img src="https://github.com/linux-can/can-logos/raw/master/png/SocketCAN-logo-60dpi.png" alt="SocketCAN logo"/>
</p>

### SocketCAN userspace utilities and tools

This repository contains some userspace utilities for Linux CAN
subsystem (aka SocketCAN):

#### Basic tools to display, record, generate and replay CAN traffic

* candump : display, filter and log CAN data to files
* canplayer : replay CAN logfiles
* cansend : send a single frame
* cangen : generate (random) CAN traffic
* cansniffer : display CAN data content differences (just 11bit CAN IDs)

#### CAN access via IP sockets
* canlogserver : log CAN frames from a remote/local host
* bcmserver : interactive BCM configuration (remote/local)
* [socketcand](https://github.com/dschanoeh/socketcand) : use RAW/BCM/ISO-TP sockets via TCP/IP sockets

#### CAN in-kernel gateway configuration
* cangw : CAN gateway userpace tool for netlink configuration

#### CAN bus measurement and testing
* canbusload : calculate and display the CAN busload
* can-calc-bit-timing : userspace version of in-kernel bitrate calculation
* canfdtest : Full-duplex test program (DUT and host part)

#### ISO-TP tools [ISO15765-2:2016 for Linux](https://github.com/hartkopp/can-isotp)
* isotpsend : send a single ISO-TP PDU
* isotprecv : receive ISO-TP PDU(s)
* isotpsniffer : 'wiretap' ISO-TP PDU(s)
* isotpdump : 'wiretap' and interpret CAN messages (CAN_RAW)
* isotpserver : IP server for simple TCP/IP <-> ISO 15765-2 bridging (ASCII HEX)
* isotpperf : ISO15765-2 protocol performance visualisation
* isotptun : create a bi-directional IP tunnel on CAN via ISO-TP

#### Log file converters
* asc2log : convert ASC logfile to compact CAN frame logfile
* log2asc : convert compact CAN frame logfile to ASC logfile
* log2long : convert compact CAN frame representation into user readable

#### Serial Line Discipline configuration (for slcan driver)
* slcan_attach : userspace tool for serial line CAN interface configuration
* slcand : daemon for serial line CAN interface configuration
* slcanpty : creates a pty for applications using the slcan ASCII protocol

### Additional Information:

*   [SocketCAN Documentation (Linux Kernel)](https://www.kernel.org/doc/Documentation/networking/can.txt)
*   [Elinux.org CAN Bus Page](http://elinux.org/CAN_Bus)
*   [Debian Package Description](https://packages.debian.org/sid/can-utils)

