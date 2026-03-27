zephyr_include_directories(include)
zephyr_library()

if(CONFIG_SHIELD_PROSPECTOR_ADAPTER_BATTERY)
  add_subdirectory(drivers/display)
endif()

zephyr_library_sources(src/prospector_brightness.c)
zephyr_library_sources(src/behaviors/behavior_prospector_brightness.c)

if(CONFIG_TOTEM_TELEMETRY)
  zephyr_library_sources(src/totem_telemetry.c)
endif()
