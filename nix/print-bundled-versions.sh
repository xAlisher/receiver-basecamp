#!/usr/bin/env bash
# #81 Print the versions of the tools in a built helper bundle, for release notes + nix/bundled-versions.txt.
# Usage: nix/print-bundled-versions.sh <bundle-dir>   (a dir from nix/build-bundle-bin.sh, holding ffplay etc.)
set -euo pipefail
B=${1:?usage: print-bundled-versions.sh <bundle-dir>}
ver() { env -i HOME=/tmp "$B/$1" "${@:2}" 2>&1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)+' | head -1; }
echo "ffmpeg=$(ver ffplay -version)"
echo "tor=$(ver tor --version)"
[ -x "$B/privoxy" ] && echo "privoxy=$(ver privoxy --version)"
# libs report their soname version; the package version is what matters — take the file soname as a proxy
[ -e "$B/libtorsocks.so.0.0.0" ] && echo "libtorsocks_soname=0.0.0 (torsocks pkg — see bundled-versions.txt)"
ls "$B"/libpulse.so.0.* 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | sed 's/^/libpulse_soname=/' || true
