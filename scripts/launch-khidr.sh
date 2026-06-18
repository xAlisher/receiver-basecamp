#!/usr/bin/env bash
# Launch the khidr demo Basecamp (radio + receiver + delivery) ISOLATED and NON-DESTRUCTIVELY —
# alongside any already-running Basecamp, without killing or disturbing it.
#
# ~/logos-basecamp-khidr.AppImage is the **268 build (ef6dca8b)** — the build where
# getClient("delivery_module") works end-to-end (the 295 build hangs it: logos-basecamp#150).
# Profile Logos-khidr has: delivery_module + radio_module (cores), radio_ui + receiver_ui (UIs).
#
# Coexistence: delivery binds TCP 60000 — only ONE delivery-using Basecamp may run. This is safe to
# run alongside a Basecamp on the *default* profile that does NOT use delivery (port 60000 free).
set -uo pipefail
APP="$HOME/logos-basecamp-khidr.AppImage"
PROF="$HOME/.local/share/Logos-khidr"
CACHE="$HOME/.cache/Logos-khidr"   # isolated cache so we never touch the running instance's qmlcache

if ss -tln 2>/dev/null | grep -q ':60000 '; then
  echo "ABORT: TCP 60000 in use — a delivery-using Basecamp is already running. Refusing to conflict." >&2
  exit 1
fi

mkdir -p "$CACHE"
export XDG_RUNTIME_DIR=/run/user/1000
export WAYLAND_DISPLAY=wayland-0
export DISPLAY=:0
XAUTH=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1); [ -n "$XAUTH" ] && export XAUTHORITY="$XAUTH"
export XDG_DATA_HOME="$PROF"
export XDG_CACHE_HOME="$CACHE"

nohup "$APP" > /tmp/khidr.log 2>&1 &
echo "khidr launched PID=$! (profile: Logos-khidr, cache: $CACHE) — booting ~80s; open Radio + Receiver."
