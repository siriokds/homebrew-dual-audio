# homebrew-dual-audio

Homebrew tap for the GPL-licensed audio modules used by
[Dual](https://github.com/siriokds/dual), kept in their own repository and
their own build so they stay separate from Dual's own binary — Dual
`dlopen()`s them at runtime, it never links them in. See Dual's own
`docs/CLAUDE/AUDIO_BACKEND_LICENSES.md` for the full reasoning.

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

## Installing

```sh
brew tap siriokds/dual-audio
brew trust siriokds/dual-audio --tap
brew install siriokds/dual-audio/dual-uade
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

**3. `brew install siriokds/dual-audio/dual-uade`**
Runs the `dual-uade` formula specifically (there may be others later, one
per module — each is installed by name). Downloads `mvtiaine/uade` (branch
`dragnet`) plus its two small build-time dependencies (`libzakalwe`,
`bencode-tools`), then `./configure && make install` for all three.

## Where things end up, and why `keg_only`

Each formula here is `keg_only`: Homebrew does **not** create its usual
symlinks into `/opt/homebrew/lib`, `/opt/homebrew/include`, etc. This is
deliberate for `dual-uade` specifically — the official `uade` formula
(stock UADE) already provides a `libuade.dylib` under those exact same
symlinked names, and `dual-uade` is meant to coexist with it, not replace
it. To find what got built:

```sh
/opt/homebrew/opt/dual-uade/lib/libuade.dylib
/opt/homebrew/opt/dual-uade/include/uade/uade.h
/opt/homebrew/opt/dual-uade/bin/uade123   # standalone CLI player, useful for a quick test
```

## Verifying `dual-uade` actually built the fork (not stock UADE)

Stock UADE doesn't support Face The Music; this fork does:

```sh
grep -c FaceTheMusic /opt/homebrew/opt/dual-uade/share/uade/eagleplayer.conf
# → 1 if this is really the fork; 0 would mean something went wrong
```

## Building a module manually, without Homebrew

Each module also has its own build script, for the same result without
going through `brew`:

```sh
git submodule update --init --recursive
modules/uade/build-macos.sh
```

Produces `modules/uade/build/macos/lib/libuade.dylib` and the matching
header. Override the install location with `PREFIX=/some/path`.

## Uninstalling / re-tapping

```sh
brew uninstall dual-uade
brew untap siriokds/dual-audio
```

`brew untap` refuses to run while a formula from the tap is still
installed — uninstall it first.

## License

**GNU General Public License v2.0** — see [LICENSE](LICENSE). The UADE
core (`modules/uade/src/`) is GPL-2.0-only; this repository, distributing
it, is licensed the same way as a whole. The `Formula/*.rb` recipes
themselves would not individually require GPL (they contain no UADE code,
only build instructions — the same reasoning Homebrew's own `homebrew-core`
uses to stay BSD-2-Clause despite hosting formulae for GPL software), but
are covered by the same repository-wide license for simplicity.

See `modules/uade/src/COPYING`, `COPYING.GPL` and `COPYING.LGPL` for
UADE's own upstream licensing notes (parts of `amigasrc/score/` are LGPL;
`players/` contains per-file licenses and copyright holders — read before
reusing an individual player outside this build).
