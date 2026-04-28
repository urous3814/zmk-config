<picture>
  <source media="(prefers-color-scheme: dark)" srcset="/docs/images/TOTEM_logo_dark.svg">
  <source media="(prefers-color-scheme: light)" srcset="/docs/images/TOTEM_logo_bright.svg">
  <img alt="TOTEM logo font" src="/docs/images/TOTEM_logo_bright.svg">
</picture>

# ZMK CONFIG FOR THE TOTEM SPLIT KEYBOARD

[Here](https://github.com/GEIGEIGEIST/totem) you can find the hardware files and build guide.\
[Here](https://github.com/GEIGEIGEIST/qmk-config-totem) you can find the QMK config for the TOTEM.

TOTEM is a 38 key column-staggered split keyboard running [ZMK](https://zmk.dev/) or [QMK](https://docs.qmk.fm/). It's meant to be used with a SEEED XIAO BLE or RP2040.


![TOTEM layout](/docs/images/TOTEM_layout.svg)



## HOW TO USE

- fork this repo
- `git clone` your repo, to create a local copy on your PC (you can use the [command line](https://www.atlassian.com/git/tutorials) or [github desktop](https://desktop.github.com/))
- adjust the totem.keymap file (find all the keycodes on [the zmk docs pages](https://zmk.dev/docs/codes/))
- `git push` your repo to your fork
- on the GitHub page of your fork navigate to "Actions"
- scroll down and unzip the `firmware.zip` archive that contains the latest firmware
- flash `settings_reset.uf2` to the devices before switching between direct and dongle builds
- connect the left half of the TOTEM to your PC, press reset twice
- the keyboard should now appear as a mass storage device
- for the Prospector dongle build, flash `totem_left_dongle.uf2`, `totem_right_dongle.uf2`, and `totem_dongle.uf2`
- for the direct no-dongle build, flash `totem_left_direct.uf2` and `totem_right_direct.uf2`
- pair the left half first and the right half second so the Prospector battery widgets stay in left-to-right order

## PROSPECTOR

- the build now includes `totem_dongle`, which uses `carrefinho/prospector-zmk-module` on the `feat/new-status-screens` branch
- the build also includes direct no-dongle artifacts where `totem_left_direct` is the central half
- the dongle target builds with `totem_dongle prospector_adapter_battery`
- the Prospector status screen is pinned to the `classic` layout
- the classic battery bar is split into left / dongle / right slots
