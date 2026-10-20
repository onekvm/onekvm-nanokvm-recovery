# Pro recovery screen

The Pro Desk uses a static 320×172 RGB565 recovery screen, with the same blue
wordmark, split layout and mode label as its initialization screen. All IPv4
addresses are visible together. The network publisher redraws only when its
status changes; it never blanks or cycles this color display.

`logo.png` is copied from `onekvm-screen-display/src/init/logo.png`.
`generate-assets.py` uses Pillow and DejaVu Sans / Sans Mono to regenerate the
checked-in `assets.h`. OE and the target need no font or image libraries.
Build target binaries through `onekvm-distro` OE/kas.
