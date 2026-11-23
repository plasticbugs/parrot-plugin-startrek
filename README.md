# Star Trek

This plugin lets Parrot Cart play Sega's 1982 Star Trek arcade game (Strategic Operations Simulator) on the ColecoVision. The cart emulates the arcade board, ported from MAME, speech and all. You supply the game's ROMs from your own copy of MAME's `startrek` set.

## What goes on the card

1. Copy `STARTREK.CVP` into the `/PLUGINS` folder.
2. Make a folder named `/STARTREK` and put MAME's `startrek.zip` in it as it is, or the files from it loose. You can also put the zip in the card's top folder as `/STARTREK.ZIP`.
3. Start Star Trek from the menu.

The plugin knows each ROM by its size and contents, so the file names don't matter. If a ROM is missing, the screen names it. Only MAME's `startrek` set works.

## Controls

On the ColecoVision controller:

- Joystick left and right turns the ship.
- Joystick up is impulse, and down is warp.
- The left fire button fires phasers.
- The right fire button fires a photon torpedo.
- Keypad * or # adds a coin.
- Keypad 1 starts a one-player game, and keypad 2 a two-player game.
- RESET on the console goes back to the menu.

On a USB pad, through a powered hub:

- The left stick or the D-pad turns the ship. The stick turns faster the further you push it.
- Up and down on the D-pad are impulse and warp.
- A fires phasers, B a photon torpedo, X is impulse and Y is warp.
- Minus adds a coin, and plus starts a one-player game.

The arcade game turns the ship with a spinner. Holding the joystick turns slowly at first, then at full speed after a third of a second.

## Picture and sound

The whole arcade screen fits on the TV, each vector a line one pixel wide in the nearest ColecoVision color. The ships on the scanner at the top right would be specks at that size, so they're drawn 2 to 3.5 times bigger, and they stay where the game puts them.

The ColecoVision shows two colors in each row of 8 pixels, so where lines of different colors meet, one color takes over. The arcade draws 40 pictures a second, and the plugin shows about 36 of them.

The speech is the arcade's speech chip, worked out sample by sample. The sound effects come from the arcade's sound board, worked out at a lower rate than MAME's. Both are mixed and played as samples on the ColecoVision's sound chip.

## Credits

Parts of this plugin are ports of MAME's code, or were checked against it. That code is under the BSD-3-Clause license, and its authors are:

- Aaron Giles, for the Sega G80 vector driver, its vector generator, its security chip, the Universal Sound Board and the speech board.
- Olivier Galibert, for the SP0250 speech chip.
- Dan Boris, Mirko Buffoni, Aaron Giles and Couriersud, for the 8035 CPU.
- Juergen Buchmueller, for the Z80 CPU, and Andrei I. Holub for its instruction list.
- Aaron Giles and O. Galibert, for MAME's scheduler, screen timing and sound streams.

You can find MAME at https://www.mamedev.org. The zip reader follows Mark Adler's puff, from zlib. USB support comes from TinyUSB by Ha Thach, under the MIT license. The plugin is built with the Raspberry Pi Pico SDK (BSD-3-Clause) and the Parrot Cart SDK (MIT).

The plugin is by Parrot Cart, and it's free. Its source is here: https://github.com/plasticbugs/parrot-plugin-startrek

Star Trek is a trademark of CBS Studios. Parrot Cart isn't connected with them or with Sega. This plugin doesn't include any game ROMs.

## Build it yourself

You need the Raspberry Pi Pico SDK 2.1.1, an Arm GNU toolchain, CMake,
Ninja, Python 3, sjasmplus and picotool. Releases are built with the Arm
GNU Toolchain 9-2019-q4 (GCC 9.2.1) and picotool 2.3.1. With those you get
the same file as the release, byte for byte.

```sh
cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
ninja -C build
```

This makes `build/STARTREK.CVP`. On Ubuntu, `tools/ci/setup.sh` installs the
same tools GitHub uses to build each release.

`CART-README.md` is the text the cart shows on the plugin's page.
`test/st/README.md` explains how the picture and sound are made, and the
tests against MAME. The rest of this repository is the Parrot Cart starter
kit, and `docs/` is its manual.

## License

The plugin is under the BSD 3-Clause license (`LICENSE`). `THIRD-PARTY.md`
lists the parts that came from elsewhere, and their licenses.
