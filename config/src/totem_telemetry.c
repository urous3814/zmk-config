#include <stdio.h>

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/event_manager.h>
#include <zmk/keymap.h>

#include <zmk/events/battery_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/events/split_central_status_changed.h>

LOG_MODULE_REGISTER(totem_telemetry, CONFIG_ZMK_LOG_LEVEL);

#define TELEMETRY_UART_NODE DT_CHOSEN(zmk_totem_telemetry_uart)
BUILD_ASSERT(DT_NODE_EXISTS(TELEMETRY_UART_NODE), "Totem telemetry UART chosen node is missing");

#define TOTEM_TELEMETRY_BLE_SERVICE_UUID_VAL                                                   \
    BT_UUID_128_ENCODE(0x54204e5b, 0xe1db, 0x4400, 0x8906, 0x8148001ddf99)
#define TOTEM_TELEMETRY_BLE_CHAR_UUID_VAL                                                      \
    BT_UUID_128_ENCODE(0xe70f015f, 0xd378, 0x4aa6, 0x97d0, 0x498c3fc6bde8)

#define TOTEM_TELEMETRY_BLE_VERSION 1U
#define TOTEM_TELEMETRY_BLE_UNKNOWN_LEVEL 0xFFU

static const struct device *const telemetry_uart = DEVICE_DT_GET(TELEMETRY_UART_NODE);

BUILD_ASSERT(CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS <= 8,
             "Totem BLE telemetry supports up to 8 split peripherals");

struct totem_telemetry_ble_payload {
    uint8_t version;
    uint8_t active_layer_index;
    uint8_t local_battery;
    uint8_t peripheral_count;
    uint8_t peripheral_connected_mask;
    uint8_t peripheral_battery[CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS];
};

struct totem_telemetry_state {
    uint8_t active_layer_index;
    uint8_t local_battery;
    uint8_t peripheral_battery[CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS];
    bool peripheral_battery_known[CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS];
    bool peripheral_connected[CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS];
};

static struct totem_telemetry_state telemetry_state = {
    .local_battery = 0,
};

static struct k_work_delayable telemetry_emit_work;
static struct totem_telemetry_ble_payload telemetry_ble_payload = {
    .version = TOTEM_TELEMETRY_BLE_VERSION,
    .peripheral_count = CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS,
};
static bool telemetry_ble_notify_enabled;

static void telemetry_schedule_emit(k_timeout_t delay);

static void telemetry_refresh_state(void) {
    telemetry_state.active_layer_index = zmk_keymap_highest_layer_active();
    telemetry_state.local_battery = zmk_battery_state_of_charge();
}

static void telemetry_fill_ble_payload(void) {
    telemetry_ble_payload.version = TOTEM_TELEMETRY_BLE_VERSION;
    telemetry_ble_payload.active_layer_index = telemetry_state.active_layer_index;
    telemetry_ble_payload.local_battery = telemetry_state.local_battery;
    telemetry_ble_payload.peripheral_count = CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS;
    telemetry_ble_payload.peripheral_connected_mask = 0;

    for (int i = 0; i < CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS; i++) {
        if (telemetry_state.peripheral_connected[i]) {
            telemetry_ble_payload.peripheral_connected_mask |= (1U << i);
        }

        telemetry_ble_payload.peripheral_battery[i] = telemetry_state.peripheral_battery_known[i]
                                                           ? telemetry_state.peripheral_battery[i]
                                                           : TOTEM_TELEMETRY_BLE_UNKNOWN_LEVEL;
    }
}

static ssize_t telemetry_read_ble_payload(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                          void *buf, uint16_t len, uint16_t offset) {
    telemetry_refresh_state();
    telemetry_fill_ble_payload();

    return bt_gatt_attr_read(conn, attr, buf, len, offset, &telemetry_ble_payload,
                             sizeof(telemetry_ble_payload));
}

static void telemetry_ble_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    ARG_UNUSED(attr);

    telemetry_ble_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    telemetry_schedule_emit(K_NO_WAIT);
}

BT_GATT_SERVICE_DEFINE(
    totem_telemetry_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(TOTEM_TELEMETRY_BLE_SERVICE_UUID_VAL)),
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(TOTEM_TELEMETRY_BLE_CHAR_UUID_VAL),
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_READ_ENCRYPT,
                           telemetry_read_ble_payload, NULL, &telemetry_ble_payload),
    BT_GATT_CCC(telemetry_ble_ccc_cfg_changed,
                BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT));

static bool telemetry_uart_ready(void) {
    if (!device_is_ready(telemetry_uart)) {
        return false;
    }

    uint32_t dtr = 0;
    if (uart_line_ctrl_get(telemetry_uart, UART_LINE_CTRL_DTR, &dtr) != 0) {
        return false;
    }

    return dtr != 0;
}

static void telemetry_write_string(const char *str) {
    for (size_t i = 0; str[i] != '\0'; i++) {
        uart_poll_out(telemetry_uart, str[i]);
    }
}

static void telemetry_write_json_string(const char *str) {
    uart_poll_out(telemetry_uart, '"');

    for (size_t i = 0; str[i] != '\0'; i++) {
        switch (str[i]) {
        case '\\':
        case '"':
            uart_poll_out(telemetry_uart, '\\');
            uart_poll_out(telemetry_uart, str[i]);
            break;
        case '\n':
            telemetry_write_string("\\n");
            break;
        case '\r':
            telemetry_write_string("\\r");
            break;
        case '\t':
            telemetry_write_string("\\t");
            break;
        default:
            uart_poll_out(telemetry_uart, str[i]);
            break;
        }
    }

    uart_poll_out(telemetry_uart, '"');
}

static void telemetry_write_snapshot(void) {
    char num_buf[16];
    const char *layer_name =
        zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(telemetry_state.active_layer_index));

    telemetry_write_string("{\"type\":\"snapshot\",\"layer\":{\"index\":");
    snprintf(num_buf, sizeof(num_buf), "%u", telemetry_state.active_layer_index);
    telemetry_write_string(num_buf);
    telemetry_write_string(",\"name\":");
    telemetry_write_json_string(layer_name != NULL ? layer_name : "");
    telemetry_write_string("},\"battery\":{\"local\":");
    snprintf(num_buf, sizeof(num_buf), "%u", telemetry_state.local_battery);
    telemetry_write_string(num_buf);
    telemetry_write_string(",\"peripherals\":[");

    for (int i = 0; i < CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS; i++) {
        if (i > 0) {
            telemetry_write_string(",");
        }

        telemetry_write_string("{\"slot\":");
        snprintf(num_buf, sizeof(num_buf), "%d", i);
        telemetry_write_string(num_buf);
        telemetry_write_string(",\"level\":");
        if (telemetry_state.peripheral_battery_known[i]) {
            snprintf(num_buf, sizeof(num_buf), "%u", telemetry_state.peripheral_battery[i]);
            telemetry_write_string(num_buf);
        } else {
            telemetry_write_string("-1");
        }
        telemetry_write_string(",\"connected\":");
        telemetry_write_string(telemetry_state.peripheral_connected[i] ? "true" : "false");
        telemetry_write_string("}");
    }

    telemetry_write_string("]}}\n");
}

static void telemetry_emit_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    telemetry_refresh_state();

    if (telemetry_uart_ready()) {
        telemetry_write_snapshot();
    }

    if (telemetry_ble_notify_enabled && zmk_ble_active_profile_is_connected()) {
        telemetry_fill_ble_payload();

        int err =
            bt_gatt_notify(NULL, &totem_telemetry_svc.attrs[1], &telemetry_ble_payload,
                           sizeof(telemetry_ble_payload));
        if (err != 0 && err != -ENOTCONN) {
            LOG_WRN("Failed to notify BLE telemetry (%d)", err);
        }
    }

    k_work_reschedule(&telemetry_emit_work, K_MSEC(CONFIG_TOTEM_TELEMETRY_INTERVAL_MS));
}

static void telemetry_schedule_emit(k_timeout_t delay) {
    k_work_reschedule(&telemetry_emit_work, delay);
}

static int totem_telemetry_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *layer_ev = as_zmk_layer_state_changed(eh);
    if (layer_ev != NULL) {
        telemetry_state.active_layer_index = zmk_keymap_highest_layer_active();
        telemetry_schedule_emit(K_NO_WAIT);
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_battery_state_changed *local_battery_ev = as_zmk_battery_state_changed(eh);
    if (local_battery_ev != NULL) {
        telemetry_state.local_battery = local_battery_ev->state_of_charge;
        telemetry_schedule_emit(K_NO_WAIT);
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_peripheral_battery_state_changed *peripheral_battery_ev =
        as_zmk_peripheral_battery_state_changed(eh);
    if (peripheral_battery_ev != NULL &&
        peripheral_battery_ev->source < CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS) {
        telemetry_state.peripheral_battery[peripheral_battery_ev->source] =
            peripheral_battery_ev->state_of_charge;
        telemetry_state.peripheral_battery_known[peripheral_battery_ev->source] = true;
        telemetry_schedule_emit(K_NO_WAIT);
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_split_central_status_changed *split_status_ev =
        as_zmk_split_central_status_changed(eh);
    if (split_status_ev != NULL && split_status_ev->slot < CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS) {
        telemetry_state.peripheral_connected[split_status_ev->slot] = split_status_ev->connected;
        telemetry_schedule_emit(K_NO_WAIT);
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (as_zmk_usb_conn_state_changed(eh) != NULL) {
        telemetry_schedule_emit(K_MSEC(50));
        return ZMK_EV_EVENT_BUBBLE;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(totem_telemetry, totem_telemetry_listener);
ZMK_SUBSCRIPTION(totem_telemetry, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(totem_telemetry, zmk_battery_state_changed);
ZMK_SUBSCRIPTION(totem_telemetry, zmk_peripheral_battery_state_changed);
ZMK_SUBSCRIPTION(totem_telemetry, zmk_split_central_status_changed);
ZMK_SUBSCRIPTION(totem_telemetry, zmk_usb_conn_state_changed);

static int totem_telemetry_init(void) {
    if (!device_is_ready(telemetry_uart)) {
        LOG_WRN("Telemetry UART device is not ready");
    }

    telemetry_refresh_state();
    telemetry_fill_ble_payload();

    k_work_init_delayable(&telemetry_emit_work, telemetry_emit_work_handler);
    telemetry_schedule_emit(K_MSEC(1000));

    return 0;
}

SYS_INIT(totem_telemetry_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
