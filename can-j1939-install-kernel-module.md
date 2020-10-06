# can-j1939 kernel module installation #



### Problem

You already have **can0** or **vcan0** up and working, **can-utils** downloaded and compiled to **~/can/can-utils** and you can send and receive frames without problems. However, when you want to bring up **can-j1939** you get error like this:

```bash
avra@vm-debian:~/can/can-utils$ sudo modprobe can-j1939
modprobe: FATAL: Module can-j1939 not found in directory /lib/modules/5.7.0.0.bpo.2-amd64
```

and also this:

```bash
avra@vm-debian:~/can/can-utils$ testj1939
testj1939: socket(j1939): Protocol not supported
```



### Solution

Above errors mean that **can-j1939** was not enabled in your kernel and you need to compile it manually. There are several ways to do it. Any Linux kernel since 5.4 has **can-j1939** module, but you will probably want to install fresher version, which leads to downloading kernel sources, enabling **can-j1939** module, recompiling kernel and installing it. I will be using Debian 10.5 x64 (buster testing) virtual machine.



#### 1. Download kernel source ####

We will download Debian patched kernel 5.8. First update your sources

```
avra@vm-debian:~$ sudo apt update
```

and then look at available Debian patched kernel source packages

```
avra@vm-debian:~$ apt-cache search linux-source
linux-source-4.19 - Linux kernel source for version 4.19 with Debian patches
linux-source - Linux kernel source (meta-package)
linux-source-5.4 - Linux kernel source for version 5.4 with Debian patches
linux-source-5.5 - Linux kernel source for version 5.5 with Debian patches
linux-source-5.6 - Linux kernel source for version 5.6 with Debian patches
linux-source-5.7 - Linux kernel source for version 5.7 with Debian patches
linux-source-5.8 - Linux kernel source for version 5.8 with Debian patches
```

If kernel 5.8 does not show in your linux-sources list (it shows above in mine since I have already upgraded stock 4.19 kernel to backported 5.7), then you will need to add backports to your sources list. It is best to do it like this

```
echo 'deb http://deb.debian.org/debian buster-backports main contrib' | sudo tee -a /etc/apt/sources.list.d/debian-backports.list
```

Alternatively, or in case you have problems with installation of some packages, or you just want to have everything in a single list, here is what my **/etc/apt/sources.list** looks like (you will need to append at least last line to yours)

```
deb http://security.debian.org/debian-security buster/updates main contrib
deb-src http://security.debian.org/debian-security buster/updates main contrib

deb http://deb.debian.org/debian/ buster main contrib non-free
deb-src http://deb.debian.org/debian/ buster main contrib non-free

deb http://deb.debian.org/debian buster-backports main contrib
```

After adding backports in one way or another, try **sudo apt update** again, and after that **apt-cache search linux-source** should show kernel 5.8 in the list, so you can install it's source package

```
sudo apt install linux-source-5.8
```

and unpack it
```
avra@vm-debian:~$ cd /usr/src
avra@vm-debian:/usr/src$ sudo tar -xaf linux-source-5.8.tar.xz
avra@vm-debian:/usr/src$ cd linux-source-5.8
```



#### 2. Add can-j1939 module to kernel ####

First we need some packages for **menuconfig** 

```
sudo apt-get install libncurses5 libncurses5-dev
```

copy and use our old configuration to run **menuconfig**

```
avra@vm-debian:/usr/src/linux-source-5.8$ sudo cp /boot/config-$(uname -r) .config
avra@vm-debian:/usr/src/linux-source-5.8$ sudo make menuconfig
```

where we enable SAE  J1939 kernel module as shown

```
	- Networking Support
		- Can bus subsystem support
			- <M> SAE J1939
```

Now edit **/usr/src/linux-source-5.8/.config**, find CONFIG_SYSTEM_TRUSTED_KEYS, change it as following
```
CONFIG_SYSTEM_TRUSTED_KEYS=""
```

and save the file.



#### 3. Compile and install kernel and modules

We will have to download necessary packages

```
sudo apt install build-essential libssl-dev libelf-dev bison flex
```

compile kernel (using threads to make it faster)

```
avra@vm-debian:/usr/src/linux-source-5.8$ sudo make -j $(nproc)
```

install

```
avra@vm-debian:/usr/src/linux-source-5.8$ sudo make modules_install
avra@vm-debian:/usr/src/linux-source-5.8$ sudo make install
```

and update grub

```
avra@vm-debian:/usr/src/linux-source-5.8$ sudo update-grub
avra@vm-debian:/usr/src/linux-source-5.8$ sudo reboot
```

Check if installation is correct with

```
sudo modprobe can-j1939
```

and if you get no error then you can enjoy **can-j1939**. If you get some error then you might check if this alternative command works:

```
sudo insmod /lib/modules/5.8.10/kernel/net/can/j1939/can-j1939.ko
```

If it does then all you need to do is 

```
sudo depmod -av
```

reboot once, and **modprobe** command from the above should finally work.



#### 4. Install headers if needed

You might have a problem with headers not being updated. To check that open file **/usr/include/linux/can.h** with

```
nano /usr/include/linux/can.h
```

If in the struct **sockaddr_can** you don’t see **j1939**, then header files did not upgrade and you need to do it manually

```
sudo cp /usr/src/linux-source-5.8/include/uapi/linux/can.h /usr/include/linux/can.h
sudo cp /usr/src/linux-source-5.8/include/uapi/linux/can/j1939.h /usr/include/linux/can/
```

That is the minimum for compiling some **J1939** C code, but you might want to upgrade other header files as well. That's up to you. Enjoy!
