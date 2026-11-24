# Star Trek

This plugin plays Sega's 1982 Star Trek arcade game (Strategic Operations Simulator) on the ColecoVision. It needs your own copy of the game's ROMs.

## Setup

1. Copy `STARTREK.CVP` into the `/PLUGINS` folder on the card.
2. Put the ROMs from MAME's `startrek` set in a folder named `/STARTREK`. You can copy the files loose, or copy `startrek.zip` as it is. You can also put the zip in the card's top folder as `/STARTREK.ZIP`.
3. Start Star Trek from the Plugins page in the menu.

The files can have any names, because the plugin knows each ROM by its size and contents. If a ROM is missing, the screen names it. Only MAME's `startrek` set works.

## Controls

On the ColecoVision controller:

- Joystick left and right turns the ship.
- Joystick up is impulse, and down is warp.
- The left fire button fires phasers.
- The right fire button fires a photon torpedo.
- Keypad * or # adds a coin.
- Keypad 1 starts a one-player game, and keypad 2 a two-player game.
- Press RESET on the console to go back to the menu, or hold both fire buttons and \* for 2 seconds (Select and Start on a USB pad).

On a USB pad, through a powered hub:

- The left stick or the D-pad turns the ship. The stick turns faster the further you push it.
- Up and down on the D-pad are impulse and warp.
- A fires phasers, B a photon torpedo, X is impulse and Y is warp.
- Minus adds a coin, and plus starts a one-player game.

The arcade game has a spinner to turn the ship. Holding the joystick turns slowly at first, then at full speed.

## The picture and the sound

The whole arcade screen fits on the TV. The ships on the scanner at the top right are drawn bigger than in the arcade so you can tell them apart, and they stay where the game puts them. The ColecoVision shows two colours in each row of 8 pixels, so where lines of different colours meet, one colour takes over.

The arcade draws 40 pictures a second, and the plugin shows most of them.

The speech is the arcade's own speech chip, worked out sample by sample. The sound effects come from the arcade's sound board, worked out at a lower rate. Both are mixed and played as samples on the ColecoVision's sound chip.

## Credits

Parts of this plugin are ports of MAME's code, or were checked against it. That code is under the BSD-3-Clause license, and its authors are:

- Aaron Giles, for the Sega G80 vector driver, its vector generator, its security chip, the Universal Sound Board and the speech board.
- Olivier Galibert, for the SP0250 speech chip.
- Dan Boris, Mirko Buffoni, Aaron Giles and Couriersud, for the 8035 CPU.
- Juergen Buchmueller, for the Z80 CPU, and Andrei I. Holub for its instruction list.
- Aaron Giles and O. Galibert, for MAME's scheduler, screen timing and sound streams.

You can find MAME at https://www.mamedev.org. The zip reader follows Mark Adler's puff, from zlib. USB support comes from TinyUSB by Ha Thach, under the MIT license. The plugin is built with the Raspberry Pi Pico SDK (BSD-3-Clause) and the Parrot Cart SDK (MIT).

The plugin itself is by Parrot Cart, and it's free. Its source is linked next to its download.

Star Trek is a trademark of CBS Studios. This plugin contains none of the game.
