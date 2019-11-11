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
* [socketcand](https://github.com/linux-can/socketcand) : use RAW/BCM/ISO-TP sockets via TCP/IP sockets
* [cannelloni](https://github.com/mguentner/cannelloni) : UDP/SCTP based SocketCAN tunnel

#### CAN in-kernel gateway configuration
* cangw : CAN gateway userspace tool for netlink configuration

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

#### CMake Project Generator
* Place your build folder anywhere, passing CMake the path.  Relative or absolute.
* Some examples using a build folder under the source tree root:
* Android : ``cmake -DCMAKE_TOOLCHAIN_FILE=~/Android/Sdk/ndk-bundle/build/cmake/android.toolchain.cmake -DANDROID_PLATFORM=android-21 -DANDROID_ABI=armeabi-v7a .. && make``
* Android Studio : Copy repo under your project's ``app`` folder, add ``add_subdirectory(can-utils)`` to your ``CMakeLists.txt`` file after ``cmake_minimum_required()``.  Generating project will build Debug/Release for all supported EABI types.  ie. arm64-v8a, armeabi-v7a, x86, x86_64.
* Raspberry Pi : ``cmake -DCMAKE_TOOLCHAIN_FILE=~/rpi/tools/build/cmake/rpi.toolchain.cmake .. && make``
* Linux : ``cmake -GNinja .. && ninja``
* Linux Eclipse Photon (Debug) : ``CC=clang cmake -G"Eclipse CDT4 - Unix Makefiles" ../can-utils/ -DCMAKE_BUILD_TYPE=Debug -DCMAKE_ECLIPSE_VERSION=4.8.0``
* To override the base installation directory use: ``CMAKE_INSTALL_PREFIX``
  ie. ``CC=clang cmake -DCMAKE_INSTALL_PREFIX=./out .. && make install``

### Additional Information:

*   [SocketCAN Documentation (Linux Kernel)](https://www.kernel.org/doc/Documentation/networking/can.txt)
*   [Elinux.org CAN Bus Page](http://elinux.org/CAN_Bus)
*   [Debian Package Description](https://packages.debian.org/sid/can-utils)

