## License

All additional files are released under GPL, same as the original qemu.

## Aim

We add a virtual usb avr aes128 cryption device against v1.5.3,

## Patch

To get the patch, visit
https://github.com/suxue/qemu-usb-avrk/compare/suxue:v1.5.3...usb-avrk.diff

## Usage

We make use of the qemu qdev model, so multiple devices can be added and
removed on the fly.

to activate the usb-avrk device, append these command line arguments to qemu:

    -usb
    -device usb-avrk,id=usb-avrk0,filename=avrk0
    -device usb-avrk,id=usb-avrk1,filename=avrk1

Then two files(`avrk0` and `avrk1`) will be created in local directory
which represent the states of these 2 devices, you may use

    watch 'xxd avrk0'

to inspect their contents. The first 16bytes are the keys, then the input
buffer, then output buffer, then 1 byte for led status, then 1 byte for led
flashing status. You can also refer to the definition of `AvrkDeviceState`
in `hw/usb/dev-avrkrypt.c`.

In the qemu telnet monitor, you can hotplug these devices as well, type

    device_del usb-avrk1

To remove the second device, type

    device_add usb-avrk,id=usb-avrk1,filename=avrk1

To reconnect it.
