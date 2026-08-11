#!/usr/bin/env bash
# Builds libuade.dylib from the mvtiaine/uade fork (submodule at ./src),
# for use by Dual's uade_backend.cpp — same C API as stock UADE, more
# format support. Mirrors the official Homebrew "uade" formula's recipe
# (see: brew cat uade), pointed at the fork instead of upstream. This is
# a manual/standalone alternative to Formula/dual-uade.rb in the repo
# root — that formula does the same thing via `brew install`.
#
# Output: $PREFIX/lib/libuade.dylib + $PREFIX/include/uade/uade.h
# Default PREFIX: ./build/macos
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${PREFIX:-"$ROOT/build/macos"}"
WORK="$ROOT/build/deps"

mkdir -p "$PREFIX/lib" "$WORK"
cd "$WORK"

# ── libzakalwe (UADE's own utility library) ──────────────────────────────
if [ ! -d libzakalwe ]; then
    curl -fsSL "https://gitlab.com/hors/libzakalwe/-/archive/v1.0.0/libzakalwe-v1.0.0.tar.bz2" \
        | tar xj
    mv libzakalwe-v1.0.0 libzakalwe
fi
(
    cd libzakalwe
    # Xcode 14.3+ rejects implicit function declarations by default;
    # the official formula applies the same workaround.
    if grep -q "CFLAGS = -W -Wall" Makefile; then
        sed -i '' 's/CFLAGS = -W -Wall/CFLAGS = -Wno-implicit-function-declaration -W -Wall/' Makefile
    fi
    sed -i '' 's/-Wl,-soname,\$@/-Wl/' Makefile
    ./configure --prefix="$PREFIX"
    # CC in this Makefile defaults to "cgcc" (Sparse's checker wrapper, not
    # installed here) — override it on the same invocation that builds. A
    # separate plain `make` first would still use the wrong default and fail.
    make install PREFIX="$PREFIX" CC="${CC:-cc}"
)

# ── bencodetools (UADE's config/metadata serialisation) ──────────────────
if [ ! -d bencodetools ]; then
    curl -fsSL "https://gitlab.com/heikkiorsila/bencodetools/-/archive/v1.0.1/bencodetools-v1.0.1.tar.bz2" \
        | tar xj
    mv bencodetools-v1.0.1 bencodetools
fi
(
    cd bencodetools
    ./configure --prefix="$PREFIX" --without-python
    make
    make install
)

# ── UADE itself (the fork, submodule) ─────────────────────────────────────
(
    cd "$ROOT/src"
    ./configure --prefix="$PREFIX" \
                --libzakalwe-prefix="$PREFIX" \
                --without-write-audio
    make
    make install
)

echo ""
echo "Built: $PREFIX/lib/libuade.dylib"
echo "Header: $PREFIX/include/uade/uade.h"
