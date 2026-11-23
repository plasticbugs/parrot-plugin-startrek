# Star Trek: how it works and how it's tested

The cart runs the arcade board the way MAME 0.288 emulates it: the main Z80 with the G80's wait states and scrambled writes, the Universal Sound Board's 8035, the speech board's 8035 with its SP0250, and the vector generator. It also follows MAME's scheduler, so the three CPUs take turns exactly as they do in MAME. The tests in this folder run the same game in MAME and in this code and compare the RAM, the vector RAM, the sound board's RAM and all three CPUs' registers at every frame. Through 4,000 frames of the attract mode and 8,000 frames of a game played by a bot, they match at every frame.

## The picture

MAME draws the game's vectors on a screen 1,024 units wide and 833 high, shown at 4:3. The TMS9918's 256 by 192 is 4:3 too, so the whole screen is drawn, each vector as a line one pixel wide.

At a quarter of the arcade's size, the ships on the scanner (the top right) would be blobs of three or four pixels. So while the game's screen is up, each small symbol in the scanner is drawn bigger about its own centre: the Klingons, the starbases and the torpedoes 3.5 times, and the Enterprise in its shields twice. They stay where the game puts them, keep their shapes and are clipped to the scanner's lines. The game's screen is known by the scanner's left edge, a vector at x 300 from y 416 to the top, which it draws in every frame and no other screen draws. The game's messages are yellow and some sit inside the scanner's corner, so a symbol drawn only in that yellow keeps its size. The high scores and the titles also put letters in that corner, and they keep theirs too.

Each of the board's 64 colours becomes the ColecoVision colour nearest it in OKLab, never black. Greys become grey, and full white becomes white. The dim olive of the screen's dividers is nearest dark red, the wrong hue, so it's dark yellow.

The TMS9918 has two colours in each row of 8 pixels. Where vectors of different colours share one, the last one drawn wins, except that the stars' grey gives way to any other colour.

The arcade draws 40 pictures a second, and the lists go at 59.6 a second. At each of the game's frames its vectors become the next picture, once the console has had all of the last one. Otherwise that frame isn't shown and the game runs on at its own speed. In a game that's about 36 pictures a second, and 38 in the attract mode.

At 150 MHz a list takes the cart 12.4 ms of its 16.77 ms on average, 13.9 ms in 99 lists of 100, and 14.4 ms at worst. No list was late in the tests. Most of that is the three CPUs and MAME's scheduler.

## The sound

The speech board's SP0250 makes its samples exactly as MAME's does, 10,000 a second. Each of the cart's samples, 5,962 a second, is the mean of the SP0250's samples it covers.

MAME runs the Universal Sound Board's synthesizer at 2 MHz, which is too much for the cart. Here it runs at two steps a sample, about 11,900 a second. The square waves are integrated exactly over each step, the noise generator's bit is averaged over the step, the filters are converted to the step's rate, and the filters that channel 2 switches are weighted by how long channel 2 is high in the step. Against MAME's recording of the same game (`soundcmp.sh`), this model's level is within 0.2 dB (-17.9 against -17.7 dBFS rms) and the log-spectral distance is 5.3 dB. Through the DAC, as the lists carry it, the distance is 7.6 dB.

The mix goes out as samples through the SN76489's three tones as a DAC, 100 in each list, with the lists run back to back. Each list also writes the whole sound chip, so a byte the console misreads lasts only one list.

MAME's own Star Trek is louder than an ordinary cartridge: -17.7 dBFS rms over the scripted game, where ordinary cartridges give -20.5 to -23.9 (`docs/testing.md`). The gain puts 13% of a game's samples into the DAC's soft knee, as Time Pilot's puts 11%, and Time Pilot's lists replay at -24.5 dBFS on MAME's ColecoVision. That makes these about -23.6. This one is an estimate: the replay on MAME's ColecoVision needs the ColecoVision BIOS, which wasn't available when the plugin was made. With the BIOS in `MAME_ROMPATH`, `soundcmp.sh` measures it.

## The tests

- `test/run.sh` and `sim/test.sh` run the plugin on the kit's model of the console and in the simulator. With `ST_ROMS=path/to/startrek.zip` they play a game from a card with the ROMs on it, zipped, loose and at the top of the card. Without it they check only the missing-ROM message.
- `compare.sh` and `trace.sh` check the board against MAME, frame by frame and instruction by instruction, for each of the three CPUs.
- `cart_host.c` runs the plugin's loop on the Mac and writes what the console shows next to the vectors it was made from (`-png`), and the lists' sound (`-wav`).
- `soundcmp.sh` compares the sound with MAME's and measures how loud it is.
- `armcount.sh` times the plugin's code on an Arm model at 150 MHz (`ARM_PY`: a Python with unicorn, capstone and pyelftools).

They need MAME's `startrek.zip` (`ST_ROMS`), and for the console's side the ColecoVision BIOS in `MAME_ROMPATH`. MAME runs headless. Neither file is in this repository, and everything the tests make from the ROMs goes under `$TMPDIR`.

The kit's console model expects every list to end 1,200 cycles before its TV frame. This plugin's lists run back to back, 60,036 cycles each, because their length is the sample clock. The kit's porting notes describe this for sample sound. The plugin's entries in `test/run.sh` and `sim/test.sh` allow those lines and nothing else.
