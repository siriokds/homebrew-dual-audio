#!/usr/bin/env bash
# Builds libsidplayfp.dylib from the upstream source (submodule at ./src,
# pinned to v2.16.1 — stessa versione della formula Homebrew ufficiale,
# nessun fork: qui serve solo isolare la GPL dal binario di Dual, non
# aggiungere formati). Mirrors the official Homebrew "libsidplayfp"
# formula's head-branch recipe (autoreconf + configure + make install).
#
# Output: $PREFIX/lib/libsidplayfp.dylib + $PREFIX/include/sidplayfp/*.h
# Default PREFIX: ./build/macos
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${PREFIX:-"$ROOT/build/macos"}"

mkdir -p "$PREFIX"

# configure vuole un "od" con -w (formato GNU): quello di sistema su macOS
# e' BSD e non lo supporta, serve il coreutils di Homebrew davanti nel PATH
# (stessa richiesta della formula ufficiale, che lo dichiara come build dep
# nel branch head).
if [ -d /opt/homebrew/opt/coreutils/libexec/gnubin ]; then
    PATH="/opt/homebrew/opt/coreutils/libexec/gnubin:$PATH"
fi

(
    cd "$ROOT/src"
    autoreconf --force --install --verbose
    ./configure --disable-silent-rules --prefix="$PREFIX"
    make install
)

echo ""
echo "Built: $PREFIX/lib/libsidplayfp.dylib"
echo "Header: $PREFIX/include/sidplayfp/sidplayfp.h"
