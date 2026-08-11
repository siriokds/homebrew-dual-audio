#!/usr/bin/env bash
# Compila StSoundLibrary (sorgente vendorizzato, non un submodule: il repo
# a monte non e' mantenuto da anni — vedi src/README.md/revision.txt) +
# l'adattatore direttamente in un'unica libreria dinamica. A differenza di
# UADE/sidplayfp non c'e' un sistema di build a monte da eseguire: StSound
# e' sempre stato pensato per essere compilato dentro il progetto che lo usa
# (esattamente come faceva Dual stesso, in third_party/StSoundLibrary).
#
# Output: $PREFIX/lib/libdual_stsound_plugin.dylib
# Default PREFIX: ./build/macos
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${PREFIX:-"$ROOT/build/macos"}"
SRC="$ROOT/src/StSoundLibrary"

mkdir -p "$PREFIX/lib"

clang++ -std=c++17 -shared -fPIC -O2 \
    -fvisibility=hidden -fvisibility-inlines-hidden \
    -I"$SRC" -I"$ROOT/plugin" \
    -o "$PREFIX/lib/libdual_stsound_plugin.dylib" \
    "$ROOT/plugin/dual_stsound_plugin.cpp" \
    "$SRC/digidrum.cpp" \
    "$SRC/Ymload.cpp" \
    "$SRC/Ym2149Ex.cpp" \
    "$SRC/YmUserInterface.cpp" \
    "$SRC/YmMusic.cpp" \
    "$SRC/LZH/LzhLib.cpp"

echo ""
echo "Built: $PREFIX/lib/libdual_stsound_plugin.dylib"
