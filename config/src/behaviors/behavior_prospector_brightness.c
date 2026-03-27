/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_prospector_brightness

#include <zephyr/device.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/prospector_brightness.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct behavior_prospector_brightness_config {
    int direction;
};

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_prospector_brightness_config *config = dev->config;

    return zmk_prospector_brightness_adjust(config->direction);
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_prospector_brightness_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define PROSPECTOR_BRIGHTNESS_INST(n)                                                              \
    static const struct behavior_prospector_brightness_config                                      \
        behavior_prospector_brightness_config_##n = {.direction = DT_INST_PROP(n, direction)};    \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_prospector_brightness_config_##n,      \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                      \
                            &behavior_prospector_brightness_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PROSPECTOR_BRIGHTNESS_INST)

#endif
