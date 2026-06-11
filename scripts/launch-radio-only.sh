#!/usr/bin/env bash
# Launch the RESERVED radio-only Basecamp: dedicated AppImage + minimal isolated profile
# (delivery_module + receiver_ui only).
#
# ~/logos-basecamp-radio-only.AppImage is the **268 build (ef6dca8b)** — the one where delivery
# getClient works end-to-end (discover + play). The latest 295 build hangs getClient("delivery_module")
# (platform regression, logos-basecamp#150 / skill delivery-getclient-hang-295), so the demo runs on 268.
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
