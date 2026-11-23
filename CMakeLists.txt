# Star Trek: Sega's 1982 Star Trek arcade game, from your own ROMs.
#
#   cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
#   ninja -C build                     -> build/STARTREK.CVP
#
# and copy the .CVP into the card's /PLUGINS folder (docs/getting-started.md).
# Made by tools/new-plugin.py.
cmake_minimum_required(VERSION 3.13)
include(cmake/parrotcart.cmake)
project(star_trek C CXX ASM)
parrotcart_init()

add_executable(star_trek src/main.c)
parrotcart_frames(star_trek)           # a whole picture every TV frame: the frame kernel

# The features (docs/features.md). Each one links its code, sets up the
# USB port with only what it needs, and puts its flag in the .CVP's USES:
#   PADS KEYBOARD MOUSE MIDI_IN   the USB port as a host, through a powered hub
#   MIDI_OUT                      the USB port as a MIDI device, on a computer
#   PSRAM                         the 8 MB PSRAM
#   CARD                          the card: files and saves (a plugin with
#                                 OPENS needs it to read its file)
# To add one later, add its word here and its code to src/main.c.
cvcart_features(star_trek PADS CARD)

# The plugin (docs/plugin-rules.md): the ID is yours for good; the version
# is the release's tag without its v.
cvcart_plugin(star_trek ID parrotcart.startrek NAME "Star Trek" VERSION 0.1.0 BY "Parrot Cart"
              ICON src/icon.txt README README.md FILE STARTREK.CVP
              HOME /STARTREK)
