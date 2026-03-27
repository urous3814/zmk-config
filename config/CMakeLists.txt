zephyr_include_directories(include)
zephyr_library()

zephyr_library_sources(src/prospector_brightness.c)
zephyr_library_sources(src/behaviors/behavior_prospector_brightness.c)
