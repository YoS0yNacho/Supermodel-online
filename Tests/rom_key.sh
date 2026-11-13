#!/bin/bash
# rom_key.sh <display> <key> [hold_ms]  -- press a key in the Supermodel window on that Xvfb display
d=$1; k=$2; hold=${3:-120}
w=$(DISPLAY=:$d xdotool search --name Supermodel | head -1)
DISPLAY=:$d xdotool windowactivate --sync $w 2>/dev/null
DISPLAY=:$d xdotool keydown --window $w $k
sleep $(awk "BEGIN{print $hold/1000}")
DISPLAY=:$d xdotool keyup --window $w $k
