# Star Trek

Sega's 1982 Star Trek arcade game, from your own ROMs.

## Controls

- Stick, fire buttons and keypad: shown on the TV and counted as you press them
- A USB game pad: works like the console's controller. The d-pad or left stick is the stick, B and A are the fire buttons, and X and Y are the keypad's \* and #. Its buttons and sticks show on the TV.
- RESET: back to the cart's menu

## What it needs

- For game pads, you need a powered USB hub between them and the cart, because the cart can't power them.
- Saves go in `/STARTREK/STARTREK.SAV`. Make the folder `/STARTREK` on the card first.

## Credits

- Parrot Cart plugin template and SDK, by the Parrot Cart contributors, MIT: <https://github.com/plasticbugs/parrot-plugin-template>
- TinyUSB, by Ha Thach and contributors, MIT: <https://github.com/hathach/tinyusb>
- Raspberry Pi Pico SDK, BSD 3-Clause: <https://github.com/raspberrypi/pico-sdk>

Source: put your repository's address here.

## License

BSD-3-Clause. See LICENSE. THIRD-PARTY.md lists the parts that came from other projects.

## Building

Your code is in `src/main.c`, where it says YOUR CODE. To build `build/STARTREK.CVP`:

```sh
cmake -B build -G Ninja \
  -DPICO_SDK_PATH=$HOME/pico-sdk
ninja -C build
```

- docs/getting-started.md: installing the tools, and using the card
- docs/features.md: the features and how to use them
- docs/testing.md: `test/run.sh star-trek star-trek-usb` tries it on a model of the console
- docs/simulator.md: `sim/run.sh src` plays it in a window, with sound and controls
- docs/release-checklist.md: what to check before you tag a release
