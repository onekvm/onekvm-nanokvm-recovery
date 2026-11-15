# Pro recovery screen

The Pro Desk uses a 320×172 RGB565 recovery screen, with the same blue
wordmark, split layout and mode label as its initialization screen. All IPv4
addresses are visible together. It never blanks or cycles this color display.

`onekvm-recovery-oled panel` keeps the screen up to date and puts a Reboot
button beside the mode label. Turning or pressing the knob selects it (a white
ring). Tapping it, or pressing the knob once it is selected, turns it into a
red Confirm for five seconds; a second press clears the boot retry counter and
reboots, as the web page does. The button is greyed out while firmware uploads
or installs (`/run/onekvm-recovery-progress`). Without the panel command, as
with an older `recovery.zip`, the initramfs publisher redraws the static
screen with the full-width mode label when addresses change.

`logo.png` is copied from `onekvm-screen-display/src/init/logo.png`.
`generate-assets.py` uses Pillow and DejaVu Sans / Sans Mono to regenerate the
checked-in `assets.h`. OE and the target need no font or image libraries.
Build target binaries through `onekvm-distro` OE/kas.
