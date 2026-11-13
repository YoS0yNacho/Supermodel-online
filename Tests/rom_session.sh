#!/bin/bash
# Real-ROM relay session for manual/automated checks: relay.py + N Supermodel
# cabinets on one machine, each on its own Xvfb display (headless Mesa) so
# keys can be injected per cabinet with xdotool. Needs: bin/supermodel,
# Xvfb, xdotool, ImageMagick (import) for screenshots.
#
#   ROMS=/path/to/roms Tests/rom_session.sh srally2 2 [extra supermodel args]
#   LOBBY_ARGS=--no-star Tests/rom_session.sh srally2 2    # keep the ring (receive-thread forwarding)
#   Tests/rom_key.sh 91 3        # insert coin on cabinet 0 (display :91), 1 = start, Up = accelerator
#
# Logs: Tests/rom_session_out/<game>/{relay.log,cabN.log,tickets.txt}. The
# cabinets log "Async net board: link stats: ..." every 10 s.
set -u
GAME=${1:-srally2}; N=${2:-2}; shift 2 || true
EXTRA="$*"
ROOT=$(cd "$(dirname "$0")/.." && pwd)
ROMS=${ROMS:-$HOME/roms}
PORT=${PORT:-1970}
OUT=${OUT:-$ROOT/Tests/rom_session_out/$GAME}
rm -rf "$OUT"; mkdir -p "$OUT"
pkill -f "bin/supermodel" 2>/dev/null; pkill -f "relay.py --port $PORT" 2>/dev/null; pkill Xvfb 2>/dev/null; sleep 1

cd "$ROOT"
python3 relay.py --port "$PORT" ${RELAY_ARGS:-} > "$OUT/relay.log" 2>&1 &
sleep 1
python3 Tests/lobby_tickets.py --relay 127.0.0.1:$PORT --game "$GAME" --players "$N" ${LOBBY_ARGS:-} > "$OUT/tickets.txt" || { echo "ticket failure"; cat "$OUT/tickets.txt"; exit 1; }
cat "$OUT/tickets.txt"

i=0
while read -r index ticket; do
  d=$((91 + i))
  Xvfb :$d -screen 0 640x480x24 +extension GLX -nolisten tcp > "$OUT/xvfb$d.log" 2>&1 &
  sleep 0.5
  DISPLAY=:$d LIBGL_ALWAYS_SOFTWARE=1 SDL_AUDIODRIVER=dummy \
    ./bin/supermodel "$ROMS/$GAME.zip" -window -res=496,384 -no-sound -no-dsb -show-fps \
      -relay=127.0.0.1:$PORT -lobby-ticket=$ticket -log-output="$OUT/cab$i.log" -log-level=info $EXTRA \
      > "$OUT/cab$i.stdout" 2>&1 &
  i=$((i+1))
done < "$OUT/tickets.txt"
echo "launched $i cabinets; logs in $OUT"
