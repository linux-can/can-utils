# CAN utilities based on SocketCAN

````can-utils```` is an extremely versatile collection of CAN utilities to sniff, transmit, record, generate, and replay CAN messages. ````can-utils```` is based on a set of open source CAN drivers and networking stack called SocketCAN, contributed by Volkswagen Research to the Linux Kernel.

For a quick review of CAN, please follow [this Wikipedia link](https://en.wikipedia.org/wiki/CAN_bus) which provides great background information and will make life easier as you get started.

## Prerequisites
To use can-utils using simply a Virtual CAN or vcan, you just need:

*   computer running linux

To trasnmit and receive CAN messages with a physical CAN devic, you will need:

*   computer running linux
*   CAN hardware with a USB serial cable

_If you are using OSX or Windows, you will need to use a virtual machine (VM) within your computer such as [Virtual Box](https://www.virtualbox.org/wiki/Downloads) which is free to dowload._ 

## Installation
To install, open your terminal and type:
```
$ sudo apt-get install can-utils
```
### Usage
In order to start transmitting or receiving CAN messages, you first need to link the USB serial port on your computer to the target CAN device you are trying to communicate with.

##### Step 1

Connect your CAN device to the USB port on your computer.

##### Step 2
Search for devices connected to a USB port:
````
ls /dev/ttyACM*
````
This should list available serial connected USB ports (hopefully just 1):
`````
/dev/ttyACM0
`````
##### Step 3
Use `slcand` to link that port to `can0` interface from SocketCAN.  If your port is `/dev/ttyACM0`, you can use:
````
sudo slcand -o -c -s6 /dev/ttyACM0 can0
````
otherwise replace the `/dev/ttyACM0` with `/dev/ttyACM*` and it should work.

##### Step 4
Bring up the CAN network with:
````
sudo ifconfig can0 up
````

##### Step 5
Sniff all the CAN messages!
````
cansniffer -cae can0
````

## CAN Bus bitrate speeds

Here's a lit of the CAN bus speed parameters that can be passed to slcand:

*   `-s0` kbit/s
*   `-s1` 20 kbit/s
*   `-s2` 50 kbit/s
*   `-s3` 100 kbit/s
*   `-s4` 125 kbit/s
*   `-s5` 250 kbit/s
*   `-s6` 500 kbit/s
*   `-s7` 800 kbit/s
*   `-s8` 1 Mbit/s

## Other useful userspace utilities within this repo:

*   asc2log
*   bcmserver
*   canbusload
*   can-calc-bit-timing
*   candump
*   canbusload
*   canfdtest
*   cangen
*   cangw
*   canlogserver
*   canplayer
*   cansend
*   cansniffer
*   isotpdump
*   isotprecv
*   isotpperf
*   isotpsend
*   isotpserver
*   isotpsniffer
*   isotptun
*   log2asc
*   log2long
*   slcan_attach
*   slcand
*   slcanpty

## Additional Information:

*   [SocketCAN Documentation](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/Documentation/networking/can.txt)
*   [Elinux.org CAN Bus Page](http://elinux.org/CAN_Bus)


## Contributing

Please submit issues or pull requests directly to this repo.

## Versioning

Versioning is based on the can-utils debian package: [https://packages.debian.org/sid/can-utils](https://packages.debian.org/sid/can-utils)

## Authors

* **Oliver Hartkopp** - *Initial work* - [hartkopp](https://github.com/hartkopp)
* **Marc Kleine-Budde** - *Initial work* - [marckleinebudde](https://github.com/marckleinebudde)

See also the list of [contributors](https://github.com/linux-can/can-utils/graphs/contributors) who participated in this project.

## License

This project is licensed under the MIT License - see the [LICENSE.md](LICENSE.md) file for details


