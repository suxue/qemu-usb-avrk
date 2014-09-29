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
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

static const char *current_dir;
static size_t current_dir_length;


#define VendorOutRequest ((USB_DIR_OUT|USB_TYPE_VENDOR|USB_RECIP_DEVICE)<<8)
#define VendorInRequest ((USB_DIR_IN|USB_TYPE_VENDOR|USB_RECIP_DEVICE)<<8)

#ifdef DEBUG_Avrk
#define DPRINTF(fmt, ...) \
do { printf("usb-avrk: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#endif

typedef struct {
    unsigned char key[16];
    unsigned char buf[16];
    unsigned char outbuf[16];
    unsigned char led;
    unsigned char flash;
} AvrkDeviceState;

typedef struct {
    USBDevice dev;
    char *filename;
    AvrkDeviceState *state;
} USBAvrkState;

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT_SERIAL,
    STR_SERIALNUMBER,
};

static const USBDescStrings desc_strings = {
    [STR_MANUFACTURER]    = "SoCS ANU",
    [STR_PRODUCT_SERIAL]  = "OpSys A2",
    [STR_SERIALNUMBER]    = "0",
};

static const USBDescIface desc_iface0 = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 0,
    .bInterfaceClass               = USB_CLASS_VENDOR_SPEC,
    .bInterfaceSubClass            = USB_SUBCLASS_UNDEFINED,
    .bInterfaceProtocol            = 0x00,
};

static const USBDescDevice desc_device = {
    .bcdUSB                        = 0x0110, // device version number
    .bMaxPacketSize0               = 8,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .bmAttributes          = 0x80,
            .bMaxPower             = 25,
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
        case REQ_LED_ON | VendorOutRequest:
            s->state->led = 1;
            break;
        case REQ_LED_OFF | VendorOutRequest:
            s->state->led = 0;
            break;
        case REQ_LED_CTL | VendorOutRequest:
            if (value)
                s->state->flash = 1;
            else
                s->state->flash = 0;
            break;
        case REQ_CHANGE_KEY | VendorOutRequest:
            memcpy(s->state->key, data, 16);
            break;
        case REQ_CHANGE_KEY | VendorInRequest:
            memcpy(data, s->state->key, 16);
            p->actual_length = 16;
            break;
        case REQ_UPLOAD_A | VendorOutRequest:
        case REQ_UPLOAD_B | VendorOutRequest:
        case REQ_UPLOAD_C | VendorOutRequest:
        case REQ_UPLOAD_D | VendorOutRequest:
            {
                request &= 0xff;
                request -= REQ_UPLOAD_A;
                memcpy(s->state->buf + 4 * request, &value, 2);
                memcpy(s->state->buf + 4 * request + 2, &index, 2);
                break;
            }
        case REQ_START_ENCRYPT | VendorOutRequest:
            AES128_ECB_encrypt(s->state->buf, s->state->key, s->state->outbuf);
            break;
        case REQ_DOWNLOAD_A | VendorInRequest:
        case REQ_DOWNLOAD_B | VendorInRequest:
            {
                request &= 0xff;
                request -= REQ_DOWNLOAD_A;
                memcpy(data, s->state->outbuf + request * 8, 8);
                p->actual_length = 8;
                break;
            }
        case REQ_STATUS | VendorInRequest:
            {
                data[0] = 0;
                data[1] = s->state->flash;
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
    DEFINE_PROP_STRING("filename", USBAvrkState, filename),
    DEFINE_PROP_END_OF_LIST(),
};

static void usb_avrk_handle_reset(USBDevice *dev)
{
    /*USBAvrkState *s = (USBAvrkState *)dev;*/

    DPRINTF("Reset\n");
}

static int usb_avrk_initfn(USBDevice *dev)
{
#define ERROR_REPORT(msg) do {\
        error_report("%s:%d: %s:%s", __FILE__, __LINE__, msg, strerror(errno));\
        return -1; \
    } while (0)
#define ERROR_OUT(msg) do { close(fd); ERROR_REPORT(msg); } while (0)

    static char default_filename[] = "avrk_state";
    USBAvrkState *s = DO_UPCAST(USBAvrkState, dev, dev);
    struct stat stat;
    int flash_device = 0;
    if (!s->filename)
        s->filename = default_filename;

    char *filename;
    if (s->filename[0] == '/')
        filename = s->filename;
    else {
        int filename_length = strlen(s->filename);
        filename = malloc(current_dir_length + filename_length + 2);
        if (filename == NULL)
            ERROR_REPORT("malloc");
        memcpy(filename, current_dir, current_dir_length);
        filename[current_dir_length] = '/';
        memcpy(filename + current_dir_length + 1, s->filename, filename_length + 1);
    }

    int fd = open(filename, O_RDWR | O_CREAT, 0644);
    if (filename != s->filename)
        free(filename);
    if (fd == -1) {
        ERROR_REPORT("open");
    }

    if (fstat(fd, &stat))
        ERROR_REPORT("fstat");

    if (stat.st_size != sizeof(*s->state)) {
        if (ftruncate(fd, sizeof(*s->state)))
            ERROR_OUT("ftruncate");
        else
            flash_device = 1;
    }

    s->state = mmap(NULL, sizeof(*s->state),
            PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0);
    if (s->state == MAP_FAILED)
        ERROR_OUT("mmap");

    close(fd);
    if (flash_device) {
        memset(s->state, 0, sizeof(*s->state));
        memcpy(s->state->key, "OperatingSystems", 16);
        s->state->flash = 1;
    }

    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    dev->auto_attach = 0;

    /*usb_handle_reset(dev);*/

    if (!dev->attached) {
        usb_device_attach(dev);
    }
    return 0;
#undef ERROR_OUT
}

static void usb_avrk_handle_destroy(USBDevice *dev)
{
    USBAvrkState *s = (USBAvrkState*)dev;
    munmap(s->state, sizeof(*s->state));
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
    current_dir = get_current_dir_name();
    current_dir_length = strlen(current_dir);
    type_register_static(&avrk_info);
    usb_legacy_register("usb-avrk", "avrk", usb_avrk_init);
}

type_init(usb_avrk_register_types)
