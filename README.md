# homebrew-dual-audio

Homebrew tap for the GPL-licensed audio modules used by
[Dual](https://github.com/siriokds/dual), kept in their own repository and
their own build so they stay separate from Dual's own binary. Each module
ships two things: the underlying library (`libuade.dylib`,
`libsidplayfp.dylib`, ...) and a small **plugin adapter** that wraps it
behind `dual_audio_plugin.h` — a stable C interface Dual dlopens at
runtime and never links against directly. See Dual's own
`docs/CLAUDE/AUDIO_BACKEND_LICENSES.md` for the full reasoning, and
`src/audio_plugin_abi.h` in the Dual repo for the interface itself
(identical copy to each module's `plugin/dual_audio_plugin.h` here).

One tap, one repository, multiple modules — new modules get their own
subfolder under `modules/` and their own `Formula/*.rb`, not a new
repository each.

## Modules

- **`modules/uade/`** — UADE (`mvtiaine/uade`, branch `dragnet`) built as a
  drop-in `libuade.dylib`, same public C API as stock UADE (verified
  identical `uade.h`), with additional Amiga format support: Face The
  Music, OctaMED Soundstudio/MMD3, Protracker4/Protracker IFF, MED4 (via
  conversion to MMD0), DigiBooster 3/Pro 2, ProTrekkr 1&2, NoiseTrekker 2.
  Formula: `Formula/dual-uade.rb`.

- **`modules/sidplayfp/`** — libsidplayfp (upstream, no fork — this module
  exists to isolate its GPL code from Dual's binary, not to add formats)
  built as `libsidplayfp.dylib` (ReSIDfp band-limited emulation). Formula:
  `Formula/dual-sidplayfp.rb`.

## Installing

```sh
brew tap siriokds/dual-audio
brew trust siriokds/dual-audio --tap
brew install siriokds/dual-audio/dual-uade
brew install siriokds/dual-audio/dual-sidplayfp
```

## Step by step

**1. `brew tap siriokds/dual-audio`**
Clones this repository into Homebrew's own tap directory and registers it
so `brew` knows the formulae under `Formula/` exist under the
`siriokds/dual-audio` namespace. Nothing is built yet. No URL needed:
`brew tap user/name` defaults to `https://github.com/user/homebrew-name`,
and this repository's name already follows that convention.

**2. `brew trust siriokds/dual-audio --tap`**
Homebrew refuses to run formula code from a tap it doesn't already know
until you explicitly confirm it — required for any third-party tap, not
specific to this one. The confirmation is stored locally
(`~/.homebrew/trust.json` or under `$XDG_CONFIG_HOME`), keyed by tap name:
it's a one-time thing **per machine** (verified that uninstalling and
untapping does not clear it), not per install.

**3. `brew install siriokds/dual-audio/<formula>`**
Runs one formula specifically — install only the modules you need. Each
one downloads its own upstream source plus small build-time dependencies,
builds the library, then compiles this repo's own plugin adapter against
it and installs both.

## Where things end up, and why `keg_only`

Each formula here is `keg_only`: Homebrew does **not** create its usual
symlinks into `/opt/homebrew/lib`, `/opt/homebrew/include`, etc. — every
module keeps its library AND its plugin adapter under its own
`/opt/homebrew/opt/dual-<name>/lib/`, which is exactly the convention
Dual's own loader scans (`audio_plugin_loader.cpp`: any
`/opt/homebrew/opt/dual-*/lib/*.dylib` exposing the right symbol gets
picked up automatically, no path to configure). For `dual-uade`
specifically this also avoids clashing with the official `uade` formula,
which installs a stock `libuade.dylib` under those same shared names.

```sh
/opt/homebrew/opt/dual-uade/lib/libuade.dylib
/opt/homebrew/opt/dual-uade/lib/libdual_uade_plugin.dylib   # what Dual actually loads
/opt/homebrew/opt/dual-uade/include/uade/uade.h
/opt/homebrew/opt/dual-uade/bin/uade123                     # standalone CLI player, useful for a quick test

/opt/homebrew/opt/dual-sidplayfp/lib/libsidplayfp.dylib
/opt/homebrew/opt/dual-sidplayfp/lib/libdual_sidplayfp_plugin.dylib
```

## Verifying `dual-uade` actually built the fork (not stock UADE)

Stock UADE doesn't support Face The Music; this fork does:

```sh
grep -c FaceTheMusic /opt/homebrew/opt/dual-uade/share/uade/eagleplayer.conf
# → 1 if this is really the fork; 0 would mean something went wrong
```

## Building a module manually, without Homebrew

Each module also has its own build script, for the same result without
going through `brew` (produces the library only, not the plugin adapter —
that one is only wired up in the Formula today):

```sh
git submodule update --init --recursive
modules/uade/build-macos.sh
modules/sidplayfp/build-macos.sh
```

Produces `modules/<name>/build/macos/lib/lib*.dylib` and the matching
headers. Override the install location with `PREFIX=/some/path`.

## Uninstalling / re-tapping

```sh
brew uninstall dual-uade dual-sidplayfp
brew untap siriokds/dual-audio
```

`brew untap` refuses to run while a formula from the tap is still
installed — uninstall it first.

## License

**GNU General Public License v2.0** — see [LICENSE](LICENSE). The UADE
core (`modules/uade/src/`) is GPL-2.0-only, libsidplayfp
(`modules/sidplayfp/src/`) is GPL-2.0-or-later; this repository,
distributing them, is licensed the same way as a whole. The `Formula/*.rb`
recipes themselves would not individually require GPL (they contain no
module code, only build instructions — the same reasoning Homebrew's own
`homebrew-core` uses to stay BSD-2-Clause despite hosting formulae for GPL
software), but are covered by the same repository-wide license for
simplicity.

See `modules/uade/src/COPYING`, `COPYING.GPL` and `COPYING.LGPL` for
UADE's own upstream licensing notes (parts of `amigasrc/score/` are LGPL;
`players/` contains per-file licenses and copyright holders — read before
reusing an individual player outside this build). See
`modules/sidplayfp/src/COPYING` for libsidplayfp's own notice.
