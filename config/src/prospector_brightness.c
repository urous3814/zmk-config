#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <zmk/prospector_brightness.h>

LOG_MODULE_REGISTER(totem_prospector_brightness, CONFIG_ZMK_LOG_LEVEL);

BUILD_ASSERT(CONFIG_TOTEM_PROSPECTOR_BRIGHTNESS_MIN <= 100,
             "Prospector minimum brightness must be between 0 and 100");

struct prospector_brightness_state {
    uint8_t brightness;
};

static struct prospector_brightness_state state = {
    .brightness =
        MAX(CONFIG_TOTEM_PROSPECTOR_BRIGHTNESS_DEFAULT, CONFIG_TOTEM_PROSPECTOR_BRIGHTNESS_MIN),
};

#if (defined(CONFIG_SHIELD_PROSPECTOR_ADAPTER) ||                                             \
     defined(CONFIG_SHIELD_PROSPECTOR_ADAPTER_BATTERY)) && DT_HAS_COMPAT_STATUS_OKAY(pwm_leds)
static const struct device *const prospector_backlight_dev = DEVICE_DT_GET_ONE(pwm_leds);
#define PROSPECTOR_BACKLIGHT_LED 0
#define PROSPECTOR_HAS_BACKLIGHT 1
#else
#define PROSPECTOR_HAS_BACKLIGHT 0
#endif

static int apply_brightness(uint8_t brightness) {
    state.brightness = MAX(brightness, CONFIG_TOTEM_PROSPECTOR_BRIGHTNESS_MIN);

#if PROSPECTOR_HAS_BACKLIGHT
    if (!device_is_ready(prospector_backlight_dev)) {
        LOG_WRN("Prospector backlight device is not ready");
        return -ENODEV;
    }

    return led_set_brightness(prospector_backlight_dev, PROSPECTOR_BACKLIGHT_LED, state.brightness);
#else
    ARG_UNUSED(brightness);
    return 0;
#endif
}

#if IS_ENABLED(CONFIG_SETTINGS)
static int prospector_brightness_settings_load_cb(const char *name, size_t len,
                                                  settings_read_cb read_cb, void *cb_arg) {
    const char *next;

    if (settings_name_steq(name, "state", &next) && !next) {
        if (len != sizeof(state)) {
            return -EINVAL;
        }

        int rc = read_cb(cb_arg, &state, sizeof(state));
        if (rc >= 0) {
            state.brightness = CLAMP(state.brightness, CONFIG_TOTEM_PROSPECTOR_BRIGHTNESS_MIN, 100);
            rc = apply_brightness(state.brightness);
        }

        return MIN(rc, 0);
    }

    return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(totem_prospector_brightness, "totem/prospector_brightness", NULL,
                               prospector_brightness_settings_load_cb, NULL, NULL);

static void prospector_brightness_save_work_handler(struct k_work *work) {
    settings_save_one("totem/prospector_brightness/state", &state, sizeof(state));
}

static struct k_work_delayable prospector_brightness_save_work;
#endif

static int update_and_save_brightness(uint8_t brightness) {
    int rc = apply_brightness(brightness);
    if (rc != 0) {
        return rc;
    }

#if IS_ENABLED(CONFIG_SETTINGS)
    int ret =
        k_work_reschedule(&prospector_brightness_save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
    return MIN(ret, 0);
#else
    return 0;
#endif
}

int zmk_prospector_brightness_adjust(int direction) {
    int next = state.brightness + (direction * CONFIG_TOTEM_PROSPECTOR_BRIGHTNESS_STEP);
    next = CLAMP(next, CONFIG_TOTEM_PROSPECTOR_BRIGHTNESS_MIN, 100);

    return update_and_save_brightness((uint8_t)next);
}

uint8_t zmk_prospector_brightness_get(void) { return state.brightness; }

static int zmk_prospector_brightness_init(void) {
#if IS_ENABLED(CONFIG_SETTINGS)
    k_work_init_delayable(&prospector_brightness_save_work, prospector_brightness_save_work_handler);
#endif

    if (IS_ENABLED(CONFIG_PROSPECTOR_USE_AMBIENT_LIGHT_SENSOR)) {
        LOG_WRN("Prospector brightness keys work best with ambient light sensor disabled");
    }

    return apply_brightness(state.brightness);
}

SYS_INIT(zmk_prospector_brightness_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
