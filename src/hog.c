#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/byteorder.h>

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "hog.h"

#include <zephyr/logging/log.h>
#define LOG_MODULE_NAME hog
LOG_MODULE_REGISTER(LOG_MODULE_NAME);


enum {
    HIDS_REMOTE_WAKE = BIT(0),
    HIDS_NORMALLY_CONNECTABLE = BIT(1),
};

struct hids_info {
    uint16_t version; /* version number of base USB HID Specification */
    uint8_t code; /* country HID Device hardware is localized for. */
    uint8_t flags;
} __packed;

struct hids_report {
    uint8_t id; /* report id */
    uint8_t type; /* report type */
} __packed;

static struct hids_info info = {
    .version = 0x0000,
    .code = 0x00,
    .flags = HIDS_NORMALLY_CONNECTABLE,
};

enum {
    HIDS_INPUT = 0x01,
    HIDS_OUTPUT = 0x02,
    HIDS_FEATURE = 0x03,
};

static struct hids_report input = {
    .id = 0x01,
    .type = HIDS_INPUT,
};

static uint8_t simulate_input;
static uint8_t ctrl_point;
static uint8_t report_map[] = {
    0x05, 0x01, // Usage Page (Generic Desktop)
    0x09, 0x06, // Usage (Keyboard)
    0xA1, 0x01, // Collection (Application)
    0x05, 0x07, //     Usage Page (Key Codes)
    0x19, 0xe0, //     Usage Minimum (224)
    0x29, 0xe7, //     Usage Maximum (231)
    0x15, 0x00, //     Logical Minimum (0)
    0x25, 0x01, //     Logical Maximum (1)
    0x75, 0x01, //     Report Size (1)
    0x95, 0x08, //     Report Count (8)
    0x81, 0x02, //     Input (Data, Variable, Absolute)

    0x75, 0x08, //     Report Size (8)
    0x95, 0x01, //     Report Count (1)
    0x81, 0x01, //     Input (Constant) reserved byte(1)

    0x75, 0x01, //     Report Size (1)
    0x95, 0x05, //     Report Count (5)
    0x05, 0x08, //     Usage Page (Page# for LEDs)
    0x19, 0x01, //     Usage Minimum (1)
    0x29, 0x05, //     Usage Maximum (5)
    0x91, 0x02, //     Output (Data, Variable, Absolute), Led report
    0x75, 0x03, //     Report Size (3)
    0x95, 0x01, //     Report Count (1)
    0x91, 0x01, //     Output (Data, Variable, Absolute), Led report padding

    0x75, 0x08, //     Report Size (8)
    0x95, 0x06, //     Report Count (6) - up to 6 simultaneous key codes (more than 1 optional)
    0x15, 0x00, //     Logical Minimum (0)
    0x25, 0x65, //     Logical Maximum (101)
    0x05, 0x07, //     Usage Page (Key codes)
    0x19, 0x00, //     Usage Minimum (0)
    0x29, 0x65, //     Usage Maximum (101)
    0x81, 0x00, //     Input (Data, Array) Key array(6 bytes)

    0x09, 0x05,       //     Usage (Vendor Defined)
    0x15, 0x00,       //     Logical Minimum (0)
    0x26, 0xFF, 0x00, //     Logical Maximum (255)
    0x95, 0x02,       //     Report Size (8)
    0x75, 0x08,       //     Report Count (2)
    0xB1, 0x02,       //     Feature (Data, Variable, Absolute)

    0xC0        // End Collection (Application)
};

static ssize_t read_info(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data, sizeof(struct hids_info));
}

static ssize_t read_report_map(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, report_map, sizeof(report_map));
}

static ssize_t read_report(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data, sizeof(struct hids_report));
}

static void input_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    simulate_input = (value == BT_GATT_CCC_NOTIFY) ? 1 : 0;
}

static ssize_t read_input_report(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, NULL, 0);
}

static ssize_t write_ctrl_point(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    uint8_t *value = attr->user_data;

    if (offset + len > sizeof(ctrl_point)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(value + offset, buf, len);

    return len;
}

/* HID Service Declaration */
BT_GATT_SERVICE_DEFINE(hog_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
    BT_GATT_CHARACTERISTIC(
        BT_UUID_HIDS_INFO,
        BT_GATT_CHRC_READ,
        BT_GATT_PERM_READ,
        read_info, NULL, &info
    ),
    BT_GATT_CHARACTERISTIC(
        BT_UUID_HIDS_REPORT_MAP,
        BT_GATT_CHRC_READ,
        BT_GATT_PERM_READ,
        read_report_map, NULL, NULL
    ),
    BT_GATT_CHARACTERISTIC(
        BT_UUID_HIDS_REPORT,
        (BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY),
        BT_GATT_PERM_READ_ENCRYPT,
        read_input_report, NULL, NULL
    ),
    BT_GATT_CCC(
        input_ccc_changed,
        (BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT)
    ),
    BT_GATT_DESCRIPTOR(
        BT_UUID_HIDS_REPORT_REF,
        BT_GATT_PERM_READ,
        read_report, NULL, &input
    ),
    BT_GATT_CHARACTERISTIC(
        BT_UUID_HIDS_CTRL_POINT,
        BT_GATT_CHRC_WRITE_WITHOUT_RESP,
        BT_GATT_PERM_WRITE,
        NULL, write_ctrl_point, &ctrl_point
    ),
    // BT_GATT_CHARACTERISTIC(
    //     BT_UUID_HIDS_BOOT_KB_IN_REPORT,
    //     (BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY),
    //     BT_GATT_PERM_READ_ENCRYPT,
    //     NULL, NULL, NULL
    // ),
    // BT_GATT_CHARACTERISTIC(
    //     BT_UUID_HIDS_BOOT_KB_OUT_REPORT,
    //     (BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP),
    //     (BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    //     NULL, NULL, NULL
    // ),
);


#define SW0_NODE DT_ALIAS(sw0)
static const struct gpio_dt_spec sw0 = GPIO_DT_SPEC_GET(SW0_NODE, gpios);

#define SW1_NODE DT_ALIAS(sw1)
static const struct gpio_dt_spec sw1 = GPIO_DT_SPEC_GET(SW1_NODE, gpios);

void hog_init(void)
{
    gpio_pin_configure_dt(&sw0, GPIO_INPUT);
    gpio_pin_configure_dt(&sw1, GPIO_INPUT);
}

#define KEY_A     0x04 // Keyboard a and A
#define KEY_B     0x05 // Keyboard b and B
#define KEY_RIGHT 0x4f // Keyboard Right Arrow
#define KEY_LEFT  0x50 // Keyboard Left Arrow
#define KEY_DOWN  0x51 // Keyboard Down Arrow
#define KEY_UP    0x52 // Keyboard Up Arrow

void hog_button_loop(void)
{
    for (;;) {
        if (simulate_input) {
            /* HID Report:
             * 0x0: modifier key codes (1 byte)
             * 0x1: reserved (1 byte)
             * 0x2: LEDs (1 byte)
             * 0x3: key codes (6 bytes)
             * 0xA: vendor (2 bytes)
             */
            int8_t report[10] = { 0 };

            if (gpio_pin_get_dt(&sw0)) {
                // report[3] = KEY_DOWN;
                report[3] = KEY_A;
            }
            if (gpio_pin_get_dt(&sw1)) {
                // report[3] = KEY_UP;
                report[3] = KEY_B;
            }

            bt_gatt_notify(NULL, &hog_svc.attrs[5], &report, sizeof(report));
        }
        k_sleep(K_MSEC(100));
    }
}