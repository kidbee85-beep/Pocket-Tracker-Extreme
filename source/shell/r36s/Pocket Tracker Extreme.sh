#!/bin/bash
# GPL-3.0-or-later. Standalone experimental R36S / ARM64 Linux launcher.
# Preserve frontend SDL mappings; never source PortMaster or system control scripts.

# Parameter expansion, cd, pwd and printf below are shell builtins. The first
# persistent marker must precede date, helper programs, preset work and native SDL.
PTE_SCRIPT_DIR="${BASH_SOURCE[0]%/*}"
[ "$PTE_SCRIPT_DIR" != "${BASH_SOURCE[0]}" ] || PTE_SCRIPT_DIR=.
PTE_APP_DIR="$PTE_SCRIPT_DIR/pockettracker_extreme"
if [ ! -d "$PTE_APP_DIR" ]; then
    printf '%s\n' "Pocket Tracker Extreme: App folder missing: $PTE_APP_DIR" >&2
    exit 2
fi
PTE_LOG="$PTE_APP_DIR/log.txt"
exec >> "$PTE_LOG" 2>&1 || exit 2
printf '\n=== Pocket Tracker Extreme 0.2.0-beta.2-R36S-Community-Test: shell entered ===\n'
# Persist before any command substitution or native graphics initialization.
# A forced power-off must not erase the only evidence of where startup stopped.
sync || printf '%s\n' 'WARNING: initial startup log sync failed'
cd -- "$PTE_APP_DIR" || exit 2
GAMEDIR="$PWD"
PTE_LOG="$GAMEDIR/log.txt"
pte_phase() { printf '[startup +%ss] %s\n' "$SECONDS" "$*"; }
export POCKETTRACKER_SYNC_STARTUP=1
export POCKETTRACKER_VIDEO="${POCKETTRACKER_VIDEO:-renderer}"
pte_phase "Video mode: $POCKETTRACKER_VIDEO; durable startup logging enabled"

PTE_CHILD_PID=""
PTE_CHILD_STATUS=""
PTE_TEMPS=()
PTE_READY_FILE=""
pte_child_running() {
    [ -n "$PTE_CHILD_PID" ] || return 1
    # Only the shell's own, still-running native job qualifies. A completed and
    # reaped PID must never cause a signal to a subsequently unrelated process.
    case $'\n'"$(jobs -pr)"$'\n' in
        *$'\n'"$PTE_CHILD_PID"$'\n'*) return 0;;
    esac
    return 1
}
pte_stop_child() {
    [ -n "$PTE_CHILD_PID" ] || return
    local pte_deadline=$((SECONDS + 8))
    if pte_child_running; then
        pte_phase "Stopping native process $PTE_CHILD_PID"
        kill -TERM "$PTE_CHILD_PID" 2>/dev/null || true
        while pte_child_running; do
            if [ "$SECONDS" -ge "$pte_deadline" ]; then
                pte_phase "Native process did not stop within eight seconds; ending own child"
                kill -KILL "$PTE_CHILD_PID" 2>/dev/null || true
                break
            fi
            sleep 0.1
        done
    fi
    wait "$PTE_CHILD_PID"
    PTE_CHILD_STATUS=$?
    pte_phase "Native process stopped with status $PTE_CHILD_STATUS"
    PTE_CHILD_PID=""
}
pte_cleanup() {
    local pte_rc=$?
    trap - EXIT HUP INT TERM USR1
    pte_stop_child
    local pte_temp
    for pte_temp in "${PTE_TEMPS[@]}"; do rm -f -- "$pte_temp"; done
    if [ -n "$PTE_READY_FILE" ]; then rm -f -- "$PTE_READY_FILE"; fi
    printf '=== App closed, status %s ===\n' "$pte_rc"
    exit "$pte_rc"
}
pte_signal() {
    local pte_rc="$1"
    trap '' HUP INT TERM
    pte_stop_child
    exit "$pte_rc"
}
trap pte_cleanup EXIT
trap 'pte_signal 129' HUP
trap 'pte_signal 130' INT
trap 'pte_signal 143' TERM
pte_fail() { printf 'ERROR: %s\n' "$*"; exit 2; }

# Rotate only after the first persistent entry has been written. Reopen stdout
# after moving the old file, otherwise subsequent phases would follow its inode.
PTE_LOG_SIZE="$(wc -c < "$PTE_LOG" 2>/dev/null)"
if [[ "$PTE_LOG_SIZE" =~ ^[[:space:]]*[0-9]+[[:space:]]*$ ]] && [ "$PTE_LOG_SIZE" -gt 1048576 ]; then
    mv -f -- "$PTE_LOG" "$GAMEDIR/log.previous.txt" || pte_fail "Cannot rotate startup log."
    exec >> "$PTE_LOG" 2>&1 || exit 2
    printf '\n=== Pocket Tracker Extreme 0.2.0-beta.2-R36S-Community-Test: shell entered (log rotated) ===\n'
fi
pte_phase "Standalone start; no PortMaster scripts or network checks"

# Keep the frontend's values, including an explicitly empty or custom value.
# Fallback databases are read only; no keyboard mapper or control helper runs.
if [ -z "${SDL_GAMECONTROLLERCONFIG_FILE+x}" ]; then
    for PTE_DATABASE in /tmp/gamecontrollerdb.txt "${HOME:-/userdata/system}/.config/gamecontrollerdb.txt" "$GAMEDIR/controllerdb/gamecontrollerdb.txt"; do
        if [ -f "$PTE_DATABASE" ]; then
            export SDL_GAMECONTROLLERCONFIG_FILE="$PTE_DATABASE"
            break
        fi
    done
fi
pte_phase "Frontend SDL settings retained; database fallback only when file variable was unset"

export POCKETTRACKER_HOME="$GAMEDIR/data"
mkdir -p -- "$POCKETTRACKER_HOME" || pte_fail "Cannot create data folder."
pte_phase "Data directory ready"

# The pack identifier is generated from factory paths+contents at packaging.
# The SD installer may preseed the same pack, avoiding 2304 checks on every boot.
PTE_PACK_ID=""; PTE_SEEDED_ID=""
if [ -f "$GAMEDIR/factory/presets-pack.id" ]; then
    IFS= read -r -n 65 PTE_PACK_ID < "$GAMEDIR/factory/presets-pack.id" || true
fi
if [ -f "$POCKETTRACKER_HOME/.factory-presets-pack.id" ]; then
    IFS= read -r -n 65 PTE_SEEDED_ID < "$POCKETTRACKER_HOME/.factory-presets-pack.id" || true
fi
if [[ "$PTE_PACK_ID" =~ ^[a-fA-F0-9]{64}$ ]] && [ "$PTE_PACK_ID" = "$PTE_SEEDED_ID" ] && [ -d "$POCKETTRACKER_HOME/Presets" ]; then
    pte_phase "Factory presets already prepared (matching pack identifier)"
elif [ -d "$GAMEDIR/factory/Presets" ]; then
    pte_phase "Preparing missing factory presets"
    PTE_DEST="$POCKETTRACKER_HOME/Presets"
    [ ! -L "$PTE_DEST" ] || pte_fail "Preset folder is a symbolic link; automatic preparation stopped."
    mkdir -p -- "$PTE_DEST" || pte_fail "Cannot create preset folder."
    PTE_LIST="$(mktemp "$POCKETTRACKER_HOME/.pte-preset-list.XXXXXX")" || pte_fail "Cannot create preset file list."
    PTE_TEMPS+=("$PTE_LIST")
    find "$GAMEDIR/factory/Presets" -type d -name '.*' -prune -o -type f ! -name '.*' -print0 > "$PTE_LIST" || pte_fail "Cannot read factory presets."
    PTE_SEED_COUNT=0
    while IFS= read -r -d '' PTE_SOURCE; do
        PTE_RELATIVE="${PTE_SOURCE#"$GAMEDIR/factory/Presets/"}"
        PTE_TARGET="$PTE_DEST/$PTE_RELATIVE"
        # Edited presets and existing symbolic links are never replaced.
        if [ -e "$PTE_TARGET" ] || [ -L "$PTE_TARGET" ]; then continue; fi
        PTE_PARENT="$PTE_DEST"
        PTE_REST="$PTE_RELATIVE"
        while [[ "$PTE_REST" == */* ]]; do
            PTE_COMPONENT="${PTE_REST%%/*}"; PTE_REST="${PTE_REST#*/}"
            PTE_PARENT="$PTE_PARENT/$PTE_COMPONENT"
            [ ! -L "$PTE_PARENT" ] || pte_fail "Preset subfolder is a symbolic link."
            mkdir -p -- "$PTE_PARENT" || pte_fail "Cannot create preset subfolder."
        done
        PTE_TEMP="$(mktemp "$PTE_PARENT/.pte-preset-copy.XXXXXX")" || pte_fail "Cannot prepare new preset file."
        PTE_TEMPS+=("$PTE_TEMP")
        if ! cp -- "$PTE_SOURCE" "$PTE_TEMP" || ! cmp -s -- "$PTE_SOURCE" "$PTE_TEMP"; then
            pte_fail "Cannot prepare presets. Check free space and the SD card."
        fi
        # -n preserves any user file created while the verified copy was made.
        mv -n -- "$PTE_TEMP" "$PTE_TARGET" || pte_fail "Cannot save new preset file."
        if [ -e "$PTE_TEMP" ]; then rm -f -- "$PTE_TEMP"; fi
        PTE_TEMPS=("$PTE_LIST")
        PTE_SEED_COUNT=$((PTE_SEED_COUNT + 1))
        if [ $((PTE_SEED_COUNT % 256)) -eq 0 ]; then pte_phase "Factory presets prepared: $PTE_SEED_COUNT"; fi
    done < "$PTE_LIST"
    rm -f -- "$PTE_LIST"
    PTE_TEMPS=()
    if [[ "$PTE_PACK_ID" =~ ^[a-fA-F0-9]{64}$ ]]; then
        PTE_MARKER_TEMP="$(mktemp "$POCKETTRACKER_HOME/.pte-preset-marker.XXXXXX")" || pte_fail "Cannot create preset marker."
        PTE_TEMPS+=("$PTE_MARKER_TEMP")
        printf '%s\n' "$PTE_PACK_ID" > "$PTE_MARKER_TEMP" || pte_fail "Cannot write preset marker."
        mv -f -- "$PTE_MARKER_TEMP" "$POCKETTRACKER_HOME/.factory-presets-pack.id" || pte_fail "Cannot save preset marker."
    fi
    pte_phase "Factory preset preparation complete; new files: $PTE_SEED_COUNT"
else
    pte_phase "No factory preset directory present"
fi

export POCKETTRACKER_AUDIO_FRAMES="${POCKETTRACKER_AUDIO_FRAMES:-512}"
export POCKETTRACKER_FULLSCREEN="${POCKETTRACKER_FULLSCREEN:-1}"
export POCKETTRACKER_CACERT="${POCKETTRACKER_CACERT:-$GAMEDIR/tools/cacert.pem}"
export PATH="$GAMEDIR/tools:$PATH"
PTE_BINARY="$GAMEDIR/pockettracker_extreme.aarch64"
[ -f "$PTE_BINARY" ] || pte_fail "App file pockettracker_extreme.aarch64 missing. Copy the complete package again."
chmod +x -- "$PTE_BINARY" 2>/dev/null || true
[ -x "$PTE_BINARY" ] || pte_fail "App file is not executable. Check SD-card access."
for PTE_TOOL in curl ffmpeg ffprobe yt-dlp qjs source-worker pte-source-worker pte-iphone-usb pte-iphone-pair pte-usbmuxd; do
    if [ -f "$GAMEDIR/tools/$PTE_TOOL" ]; then chmod +x -- "$GAMEDIR/tools/$PTE_TOOL" 2>/dev/null || true; fi
done
if [ ! -f "$POCKETTRACKER_CACERT" ]; then printf '%s\n' "Note: CA certificate file missing; online sources cannot be connected securely."; fi

PTE_STARTUP_TIMEOUT="${PTE_STARTUP_TIMEOUT_SECONDS:-25}"
[[ "$PTE_STARTUP_TIMEOUT" =~ ^[0-9]+$ ]] || pte_fail "Invalid startup timeout."
[ "${#PTE_STARTUP_TIMEOUT}" -le 3 ] && [ "$PTE_STARTUP_TIMEOUT" -ge 1 ] && [ "$PTE_STARTUP_TIMEOUT" -le 120 ] || pte_fail "Startup timeout must be 1 to 120 seconds."
PTE_STARTUP_TIMEOUT=$((10#$PTE_STARTUP_TIMEOUT))
PTE_READY_FILE="$GAMEDIR/.pte-startup-ready.$$.$RANDOM.$RANDOM"
[ ! -e "$PTE_READY_FILE" ] && [ ! -L "$PTE_READY_FILE" ] || pte_fail "Startup marker already exists."
export PTE_STARTUP_READY_FILE="$PTE_READY_FILE"
pte_phase "Launching native app; first-frame deadline $PTE_STARTUP_TIMEOUT seconds"
sync || true
printf 'Start: %s\nData: %s\nAudio buffer: %s frames\n' "$PTE_BINARY" "$POCKETTRACKER_HOME" "$POCKETTRACKER_AUDIO_FRAMES"
"$PTE_BINARY" "$@" &
PTE_CHILD_PID=$!
# SECONDS has one-second resolution; round outward so a just-started native
# process always receives the full configured interval before the first frame.
PTE_STARTUP_DEADLINE=$((SECONDS + PTE_STARTUP_TIMEOUT + 1))
# The parent watches its own job until the first presented frame, then uses a
# normal wait. There is no detached watchdog that could signal a reused PID.
while pte_child_running; do
    if [ -f "$PTE_READY_FILE" ] && [ ! -L "$PTE_READY_FILE" ]; then
        PTE_READY_STATE=""
        IFS= read -r -n 32 PTE_READY_STATE < "$PTE_READY_FILE" || true
        if [ "$PTE_READY_STATE" = "frame submitted" ]; then
            pte_phase "Native first frame submitted; startup watchdog complete"
            break
        fi
    fi
    if [ "$SECONDS" -ge "$PTE_STARTUP_DEADLINE" ]; then
        pte_phase "STARTUP TIMEOUT: no first presented frame; stopping native child"
        pte_stop_child
        exit 124
    fi
    sleep 0.1
done
wait "$PTE_CHILD_PID"
PTE_EXIT_STATUS=$?
PTE_CHILD_PID=""
exit "$PTE_EXIT_STATUS"
