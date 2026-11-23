# Star Trek (STARTREK.CVP): Sega's 1982 vector arcade game on the
# ColecoVision. The cart runs the arcade board (the main Z80, the Universal
# Sound Board's 8035 and the speech board's 8035 with its SP0250: src/st.c,
# from MAME 0.288) from your own ROMs on the card (/STARTREK), and
# src/stcv.c draws its vectors on the console's screen and sends them, with
# the sound, as the frame kernel's lists. RESET goes back to the menu.
#
#   cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
#   ninja -C build                     -> build/STARTREK.CVP
#
# and copy the .CVP into the card's /PLUGINS folder (docs/getting-started.md).
# Started from tools/new-plugin.py.
cmake_minimum_required(VERSION 3.13)
include(cmake/parrotcart.cmake)
project(star_trek C CXX ASM)
parrotcart_init()

# The same source makes the same file, whenever it's built: __DATE__ and
# __TIME__ come from SOURCE_DATE_EPOCH, fixed here (2026-11-23).
set(BUILD_EPOCH 1795392000)
set(CMAKE_C_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env SOURCE_DATE_EPOCH=${BUILD_EPOCH})
set(CMAKE_CXX_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env SOURCE_DATE_EPOCH=${BUILD_EPOCH})

set(EMU src/st.c src/stz80.c src/st48usb.c src/st48sp.c src/stsnd.c src/strun.c)
set(CARD src/stcard.c src/stroms.c src/swzip.c)
add_executable(star_trek src/main.c src/stcv.c ${EMU} ${CARD} ${PARROTCART_SDK}/midihello/vdp.c)
parrotcart_frames(star_trek)            # a whole picture every TV frame: the frame kernel
target_include_directories(star_trek PRIVATE src ${PARROTCART_SDK}/sdplayer)
# The emulation -O3, its state not split into a section a variable (every
# use would need its own address loaded); the picture (stcv.c) -O2; the
# card's code runs once, at the start: built for size. test/st/armcount.sh
# builds each file the same way.
set_source_files_properties(src/main.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-unused-parameter")
set_source_files_properties(${EMU} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-unused-parameter;-O3;-fno-data-sections")
set_source_files_properties(src/stcv.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-unused-parameter;-O2")
set_source_files_properties(${CARD} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-unused-parameter;-Os")
# no tracing hook in the cart's Z80; core 0's stack 4K, core 1's 2K
target_compile_definitions(star_trek PRIVATE Z80_NO_TRACE PICO_PRINTF_SUPPORT_FLOAT=0 PICO_PRINTF_SUPPORT_EXPONENTIAL=0
    PICO_STACK_SIZE=0x1000 PICO_CORE1_STACK_SIZE=0x800)

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
cvcart_plugin(star_trek ID parrotcart.startrek NAME "Star Trek" VERSION 0.1.1 BY "Parrot Cart"
              ICON src/icon.txt README CART-README.md FILE STARTREK.CVP
              HOME /STARTREK USES usb-host)
