#!/bin/sh
# setup.sh -- the set unzipped under $TMPDIR for st_host (prints the folder).
# ST_ROMS is the startrek.zip to use.
set -e
roms=${ST_ROMS:?ST_ROMS is the startrek.zip to use}
work=${ST_WORK:-${TMPDIR:-/tmp}/startrek-test}
dir="$work/set"
mkdir -p "$dir"
unzip -o -q "$roms" -d "$dir"
echo "$dir"
