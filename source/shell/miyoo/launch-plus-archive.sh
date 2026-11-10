#!/bin/sh
# TrackWerk / Miyoo PLUS ARCHIVE EXPERIMENTAL. GPL-3.0-or-later.
# Onion 4.3 Ports launch_standalone owns MainUI/audio-server restoration on return.
PTE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
cd "$PTE_DIR" || exit 1
PTE_HOME=${POCKETTRACKER_HOME:-/mnt/SDCARD/PocketTrackerExtreme}
PTE_LOG="$PTE_DIR/log.txt"
[ ! -f "$PTE_LOG" ] || cp -f "$PTE_LOG" "$PTE_DIR/log.previous.txt" 2>/dev/null
exec > "$PTE_LOG" 2>&1
printf '%s\n' 'TrackWerk / Miyoo Plus Archive experimental' "Started: $(date)"
printf '%s\n' 'Private Plus-only test. Archive/Commons software-tested; Wi-Fi hardware untested. Physical recording and YouTube are disabled.'
mkdir -p "$PTE_HOME" || exit 1
export POCKETTRACKER_HOME="$PTE_HOME"
if [ ! -e "$PTE_HOME/config.json" ]; then cp "$PTE_DIR/miyoo-config.json" "$PTE_HOME/config.json" || exit 1; fi
if [ ! -e "$PTE_HOME/config.example.json" ]; then cp "$PTE_DIR/miyoo-config.json" "$PTE_HOME/config.example.json" 2>/dev/null; fi
# Keep all firmware libraries on their own device. Only our exact mmiyoo SDL and GPL helper are
# bundled; never install a replacement libc or hardware driver.
PTE_PRIOR_LD=${LD_LIBRARY_PATH-}
# MainUI may inherit parasyte's HTTP-only curl. Prefer Onion's normal TLS runtime
# before inherited paths, while keeping our exact device SDL first.
export LD_LIBRARY_PATH="$PTE_DIR/libs:/mnt/SDCARD/.tmp_update/lib:/mnt/SDCARD/miyoo/lib${PTE_PRIOR_LD:+:$PTE_PRIOR_LD}:/config/lib:/customer/lib:/lib"
printf 'Network runtime path: %s\n' "$LD_LIBRARY_PATH"
# Onion supplies libcurl/OpenSSL/zlib. No system network library is replaced.
: "${POCKETTRACKER_CACERT:=$PTE_DIR/certs/cacert.pem}"
export POCKETTRACKER_CACERT
PTE_YEAR=$(date +%Y 2>/dev/null)
case "$PTE_YEAR" in ''|*[!0-9]*) ;; *)
 if [ "$PTE_YEAR" -lt 2020 ]; then
  printf '%s\n' 'CLOCK: Set the current date/time in Onion Clock before HTTPS. TLS verification stays on; offline music remains available.'
 fi;; esac
export SDL_VIDEODRIVER=mmiyoo SDL_AUDIODRIVER=mmiyoo
export SDL_MMIYOO_INPUT_MODE=joystick
# This framebuffer needs only the native 640x480 streaming texture. Keep optional texture caching
# modest on 128 MiB hardware; no CPU overclock/governor or system graphics setting is changed.
export SDL_MMIYOO_TEXTURE_POOL_MAX_BYTES=2097152
export PT_CONSOLE=1 POCKETTRACKER_FULLSCREEN=1
unset LD_PRELOAD
: "${miyoodir:=/mnt/SDCARD/miyoo}"
export miyoodir
PTE_AUDIO_STOP=${PTE_ONION_STOP_AUDIO:-/mnt/SDCARD/.tmp_update/script/stop_audioserver.sh}
if [ ! -f "$PTE_AUDIO_STOP" ]; then
 printf '%s\n' 'Onion audio handover script missing. Install Onion 4.3 and Ports Collection first.'
 exit 1
fi
sh "$PTE_AUDIO_STOP"
sleep 0.3
chmod +x "$PTE_DIR/pockettracker" 2>/dev/null
"$PTE_DIR/pockettracker" &
PTE_CHILD=$!
PTE_TIMEOUT=${PTE_STARTUP_TIMEOUT_SECONDS:-20}
case "$PTE_TIMEOUT" in ''|*[!0-9]*) PTE_TIMEOUT=20;; esac
[ "$PTE_TIMEOUT" -gt 0 ] && [ "$PTE_TIMEOUT" -le 60 ] || PTE_TIMEOUT=20
# Startup watchdog owns only this launcher's child. Frame-loop entry proves video/controller
# construction completed. It does not claim a physical screen/audio test.
(
 PTE_N=0
 while [ "$PTE_N" -lt "$PTE_TIMEOUT" ]; do
  kill -0 "$PTE_CHILD" 2>/dev/null || exit 0
  grep -q 'startup: video first SDL_RenderPresent returned' "$PTE_LOG" && exit 0
  sleep 1
  PTE_N=$((PTE_N+1))
 done
 kill -0 "$PTE_CHILD" 2>/dev/null || exit 0
 grep -q 'startup: video first SDL_RenderPresent returned' "$PTE_LOG" && exit 0
 printf 'Startup timeout after %s seconds; returning to Onion. See log.txt.\n' "$PTE_TIMEOUT"
 kill -TERM "$PTE_CHILD" 2>/dev/null
 sleep 1
 kill -KILL "$PTE_CHILD" 2>/dev/null
) &
PTE_WATCH=$!
trap 'kill -TERM "$PTE_CHILD" 2>/dev/null' TERM INT HUP
wait "$PTE_CHILD"
PTE_RC=$?
kill "$PTE_WATCH" 2>/dev/null
wait "$PTE_WATCH" 2>/dev/null
printf 'Exit code: %s\n' "$PTE_RC"
PTE_PANEL=/mnt/SDCARD/.tmp_update/bin/infoPanel
if [ "$PTE_RC" -ne 0 ] && [ -x "$PTE_PANEL" ]; then
 LD_LIBRARY_PATH=/mnt/SDCARD/miyoo/lib:/config/lib:/lib "$PTE_PANEL" --title 'TrackWerk' --message 'Experimental Miyoo build could not start. Details: Roms/PORTS/Games/PocketTrackerExtremePlusArchive/log.txt' > /dev/null 2>&1
fi
exit "$PTE_RC"
