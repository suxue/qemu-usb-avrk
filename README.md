## License

`dev-avrkrypt.c` is released under the LGPL.

`aes.c` and `aes.h` are verbatim copies from https://github.com/kokke/tiny-AES128-C,
which are in public domain.

## Aim

We add a virtual usb avr aes128 encryption device against qemu v1.5.3 with
the x86_64 target.

## Patch

### Get the original source as well as the patch

    git clone git@github.com:suxue/qemu-usb-avrk.git
    cd qemu-usb-avrk/
    git submodule update --init

### Download the tarball and apply this patch

    wget http://wiki.qemu-project.org/download/qemu-1.5.3.tar.bz2
    tar xvf  qemu-1.5.3.tar.bz2
    cd qemu-1.5.3/
    wget https://github.com/suxue/qemu-usb-avrk/compare/suxue:v1.5.3...usb-avrk.diff -O - | patch -Np1

## Compile qemu

http://www.linuxfromscratch.org/blfs/downloads/7.4/BLFS-BOOK-7.4-nochunks.html#qemu

## Get a usable image

People do not have enough time to build their own qemu image may want to try mine,
which includes all auxiliary scripts and a small (20Mb, with glibc-2.20,
libusb, pciutils, usbutils...) rootfs image.

    git clone https://gist.github.com/dade5b978b276e290512.git image/
    cd image/
    ./spawn.sh # this will spawn a screen session (virtual console and qemu monitor), or
    ./boot.sh SERIAL=stdio # the console of qemu will be connected to stdio
    ./ssh.sh 'zcat /proc/config.gz' > config # download the kernel configuration

## Usage

We make use of the qemu qdev model, so multiple devices can be added and
removed on the fly.

to activate the `usb-avrk` device, append these command line arguments to
`qemu`:

    -usb
    -device usb-avrk,id=usb-avrk0,filename=avrk0
    -device usb-avrk,id=usb-avrk1,filename=avrk1

Then two files(`avrk0` and `avrk1`) will be created in local directory
which represent the states of these 2 devices, you may use

    watch 'xxd avrk0'

to inspect their contents. The first 16 bytes are the keys, then the input
buffer, then output buffer, then 1 byte for led status, then 1 byte for led
flashing status. You can also refer to the definition of `AvrkDeviceState`
in `hw/usb/dev-avrkrypt.c`.

Moreover, in the qemu telnet monitor, you can hotplug these devices.

+ remove the 2nd device: `device_del usb-avrk1`.
+ reconnect the 2nd device: `device_add usb-avrk,id=usb-avrk1,filename=avrk1`.

## Todo

The echo request is not implemented yet.
