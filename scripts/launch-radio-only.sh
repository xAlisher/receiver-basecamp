#!/usr/bin/env bash
# Launch the RESERVED radio-only Basecamp: dedicated AppImage + minimal isolated profile
# (delivery_module + receiver_ui only). Minimal profile avoids the platform QRO-allocator
# degradation that makes getClient("delivery_module") hang in a busy multi-module profile
# (see basecamp-skills: ipc-client-eager-init "permanent failure mode").
#
# DO NOT point other Basecamp work at ~/logos-basecamp-radio-only.AppImage or the
# ~/.local/share/Logos-radio-only profile — they are reserved for the radio/receiver demo.
set -uo pipefail
APP="$HOME/logos-basecamp-radio-only.AppImage"
PROF="$HOME/.local/share/Logos-radio-only"

# Only one Basecamp at a time — separate profiles still share the delivery TCP port (60000).
for p in $(pgrep -f 'LogosBasecamp\.elf|logos_host\.elf|ui-host'); do kill -9 "$p" 2>/dev/null; done
sleep 3
for m in /tmp/.mount_logos-*; do fusermount -u "$m" 2>/dev/null; done
rm -rf "$HOME/.cache/Logos/LogosBasecamp/qmlcache/"

export XDG_RUNTIME_DIR=/run/user/1000
export WAYLAND_DISPLAY=wayland-0
export DISPLAY=:0
XAUTH=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1); [ -n "$XAUTH" ] && export XAUTHORITY="$XAUTH"
export XDG_DATA_HOME="$PROF"

nohup "$APP" > /tmp/radio-only.log 2>&1 &
echo "radio-only launched PID=$! (profile: Logos-radio-only) — booting ~80s; open Receiver."
