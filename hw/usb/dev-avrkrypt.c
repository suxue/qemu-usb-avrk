/*
 * Avr aes128 cryption stick Device emulation
 *
 * Copyright (c) 2014 Hao Fei.
 *
 * This code is licensed under the LGPL.
 */

#include "qemu-common.h"
#include "qemu/error-report.h"
#include "hw/usb.h"
#include "hw/usb/desc.h"
#include "sysemu/char.h"
#include "aes.h"

#define VendorOutRequest ((USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_DEVICE)<<8)
#define VendorInRequest ((USB_DIR_IN|USB_TYPE_VENDOR|USB_RECIP_DEVICE)<<8)

#ifdef DEBUG_Avrk
#define DPRINTF(fmt, ...) \
do { printf("usb-avrk: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#endif


typedef struct {
    USBDevice dev;
    unsigned char key[16];
    unsigned char buf[16];
    unsigned char outbuf[16];
} USBAvrkState;

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT_SERIAL,
    STR_SERIALNUMBER,
};

static const USBDescStrings desc_strings = {
    [STR_MANUFACTURER]    = "QEMU",
    [STR_PRODUCT_SERIAL]  = "QEMU AVRK STICK",
    [STR_SERIALNUMBER]    = "1",
};

static const USBDescIface desc_iface0 = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 2,
    .bInterfaceClass               = 0,
    .bInterfaceSubClass            = USB_SUBCLASS_UNDEFINED,
    .bInterfaceProtocol            = 0x00,
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_IN | 0x01,
            .bmAttributes          = USB_ENDPOINT_XFER_CONTROL,
            .wMaxPacketSize        = 64,
        },{
            .bEndpointAddress      = USB_DIR_OUT | 0x02,
            .bmAttributes          = USB_ENDPOINT_XFER_CONTROL,
            .wMaxPacketSize        = 64,
        },
    }
};

static const USBDescDevice desc_device = {
    .bcdUSB                        = 0x01, // device version number
    .bMaxPacketSize0               = 8,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .bmAttributes          = 0x80,
            .bMaxPower             = 50,
            .nif = 1,
            .ifs = &desc_iface0,
        },
    },
};

static const USBDesc desc_avrk = {
    .id = {
        .idVendor          = 0xf055,
        .idProduct         = 0xcec5,
        .bcdDevice         = 0x01,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT_SERIAL,
        .iSerialNumber     = STR_SERIALNUMBER,
    },
    .full = &desc_device,
    .str  = desc_strings,
};

enum Request {
    REQ_ECHO,
    REQ_STATUS,
    REQ_LED_ON,
    REQ_LED_OFF,
    REQ_UPLOAD_A,
    REQ_UPLOAD_B,
    REQ_UPLOAD_C,
    REQ_UPLOAD_D,
    REQ_DOWNLOAD_A,
    REQ_DOWNLOAD_B,
    REQ_START_ENCRYPT,
    REQ_LED_CTL,
    REQ_CHANGE_KEY,
};

static void usb_avrk_handle_control(USBDevice *dev, USBPacket *p,
               int request, int value, int index, int length, uint8_t *data)
{
    USBAvrkState *s = (USBAvrkState*)dev;
    int ret;

    DPRINTF("got control %x, value %x\n",request, value);
    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0)
        return;

    switch (request) {
        case REQ_CHANGE_KEY | VendorOutRequest:
            memcpy(s->key, data, 16);
            break;
        case REQ_CHANGE_KEY | VendorInRequest:
            memcpy(data, s->key, 16);
            p->actual_length = 16;
            break;
        case REQ_UPLOAD_A | VendorOutRequest:
        case REQ_UPLOAD_B | VendorOutRequest:
        case REQ_UPLOAD_C | VendorOutRequest:
        case REQ_UPLOAD_D | VendorOutRequest:
            {
                request &= 0xff;
                request -= REQ_UPLOAD_A;
                memcpy(s->buf + 4 * request, &value, 2);
                memcpy(s->buf + 4 * request + 2, &index, 2);
                break;
            }
        case REQ_START_ENCRYPT | VendorOutRequest:
            AES128_ECB_encrypt(s->buf, s->key, s->outbuf);
            break;
        case REQ_DOWNLOAD_A | VendorInRequest:
        case REQ_DOWNLOAD_B | VendorInRequest:
            {
                request &= 0xff;
                request -= REQ_DOWNLOAD_A;
                memcpy(data, s->outbuf + request * 8, 8);
                p->actual_length = 8;
                break;
            }
        case REQ_STATUS | VendorInRequest:
            {
                data[0] = data[1] = 0;
                p->actual_length = 2;
                break;
            }
        default:
            break;
    }

}

static const VMStateDescription vmstate_usb_avrk = {
    .name = "usb-avrk",
    .unmigratable = 1,
};

static Property avrk_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void usb_avrk_handle_reset(USBDevice *dev)
{
    /*USBAvrkState *s = (USBAvrkState *)dev;*/

    DPRINTF("Reset\n");
}

static int usb_avrk_initfn(USBDevice *dev)
{
    USBAvrkState *s = DO_UPCAST(USBAvrkState, dev, dev);
    memcpy(s->key, "OperatingSystems", 16);

    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    dev->auto_attach = 0;

    /*usb_handle_reset(dev);*/

    if (!dev->attached) {
        usb_device_attach(dev);
    }
    return 0;
}

static void usb_avrk_handle_destroy(USBDevice *dev)
{
}

static void usb_avrk_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->init = usb_avrk_initfn;
    uc->product_desc   = "QEMU USB Avrkrypt";
    uc->usb_desc       = &desc_avrk;
    uc->handle_reset   = usb_avrk_handle_reset;
    uc->handle_control = usb_avrk_handle_control;
    uc->handle_destroy = usb_avrk_handle_destroy;
    dc->vmsd = &vmstate_usb_avrk;
    dc->props = avrk_properties;
}

static USBDevice *usb_avrk_init(USBBus *bus, const char *filename)
{
    USBDevice *dev;

    dev = usb_create(bus, "usb-avrk");
    if (!dev) {
        return NULL;
    }
    qdev_prop_set_uint16(&dev->qdev, "vendorid", desc_avrk.id.idVendor);
    qdev_prop_set_uint16(&dev->qdev, "productid", desc_avrk.id.idProduct);
    qdev_init_nofail(&dev->qdev);

    return dev;
}


static const TypeInfo avrk_info = {
    .name          = "usb-avrk",
    .parent        = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBAvrkState),
    .class_init    = usb_avrk_class_initfn,
};

static void usb_avrk_register_types(void)
{
    type_register_static(&avrk_info);
    usb_legacy_register("usb-avrk", "avrk", usb_avrk_init);
}

type_init(usb_avrk_register_types)
