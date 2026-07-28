#!/usr/bin/env bash
# receiver#68 — headless regression guard for the macOS deps-install block.
#
# Bug: brew's "[y/n]" dependency-upgrade prompt reads stdin, which in a pasted block is the NEXT
# pasted line (a `launchctl setenv …`) → "Invalid input" ×3, nothing installs. Fix: every `brew
# install` in the deps card must be non-interactive (HOMEBREW_NO_INSTALL_UPGRADE=1 +
# HOMEBREW_NO_AUTO_UPDATE=1) AND self-contained (`</dev/null` detaches stdin) so no following pasted
# line can be consumed as a prompt answer.
#
# This test needs no build/GUI — it inspects the three sources that emit the block:
#   1. src/qml/Main.qml           (macFastPath — the overlay's one-command block, the exact #68 repro)
#   2. src/receiver_ui_backend.cpp(publishDeps() installCmd — the per-missing-package variant)
#   3. README.md                  (the documented macOS deps block)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QML="$ROOT/src/qml/Main.qml"
BACKEND="$ROOT/src/receiver_ui_backend.cpp"
README="$ROOT/README.md"

# The canonical, byte-identical brew-install invocation shared by the overlay block and the README.
CANON='HOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_INSTALL_UPGRADE=1 brew install tor ffmpeg privoxy </dev/null'

fail=0
pass() { printf '  ok   %s\n' "$1"; }
bad()  { printf '  FAIL %s\n' "$1"; fail=1; }

echo "== (a) generator block == README block (byte-for-byte, canonical invocation) =="
# The overlay macFastPath and the README structure their surrounding steps differently, but the
# brew-install invocation itself must be byte-identical across both. Assert the exact canonical line
# is present verbatim in each source.
if grep -qF "$CANON" "$QML";    then pass "Main.qml contains the canonical brew invocation"; else bad "Main.qml missing canonical brew invocation"; fi
if grep -qF "$CANON" "$README"; then pass "README contains the canonical brew invocation";  else bad "README missing canonical brew invocation";  fi

echo "== (b) non-interactive AND self-contained: every 'brew install' is guarded =="
# Any line that runs `brew install` MUST carry both HOMEBREW_NO_* flags and detach stdin (</dev/null),
# so no following pasted line can be read as a prompt answer. Scan all three sources; a bare
# `brew install …` with no guard is the #68 regression.
for f in "$QML" "$BACKEND" "$README"; do
  # match real invocations (`brew install`), skip prose mentions like "get Homebrew"
  while IFS= read -r line; do
    trimmed="${line#"${line%%[![:space:]]*}"}"
    # skip C++/QML source comments ("// …") — those only *describe* the fix, they don't run brew
    [[ "$trimmed" == //* ]] && continue
    # ignore the nix line's descriptive prefix; we only care about the brew invocation on the line
    if [[ "$line" == *"brew install"* ]]; then
      if [[ "$line" == *"HOMEBREW_NO_INSTALL_UPGRADE=1"* \
         && "$line" == *"HOMEBREW_NO_AUTO_UPDATE=1"* \
         && "$line" == *"</dev/null"* ]]; then
        pass "$(basename "$f"): guarded — ${line#"${line%%[![:space:]]*}"}"
      else
        bad  "$(basename "$f"): UNGUARDED brew install — ${line#"${line%%[![:space:]]*}"}"
      fi
    fi
  done < <(grep -n 'brew install' "$f" | sed 's/^[0-9]*://')
done

echo
if [[ "$fail" -ne 0 ]]; then
  echo "DEPS-CARD TEST: FAIL"
  exit 1
fi
echo "DEPS-CARD TEST: PASS"
