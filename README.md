# homebrew-dual-uade

Homebrew tap for [dual-uade](https://github.com/siriokds/dual-uade) —
UADE (`mvtiaine/uade`, branch `dragnet`) built as a drop-in `libuade.dylib`
for [Dual](https://github.com/siriokds/dual), with extra Amiga format
support beyond stock UADE.

```sh
brew tap siriokds/dual-uade
brew trust siriokds/dual-uade --tap
brew install siriokds/dual-uade/dual-uade
```

## Step by step

**1. `brew tap siriokds/dual-uade`**
Clones this repository into Homebrew's own tap directory
(`$(brew --repository)/Library/Taps/siriokds/homebrew-dual-uade`) and
registers it so `brew` knows a formula named `dual-uade` exists under the
`siriokds/dual-uade` namespace. Nothing is built or installed yet — this
step only makes the *recipe* (`Formula/dual-uade.rb`) visible to Homebrew.
No URL needed: `brew tap user/name` defaults to
`https://github.com/user/homebrew-name`, and this repository's name already
follows that convention.

**2. `brew trust siriokds/dual-uade --tap`**
Homebrew refuses to run formula code from a tap it doesn't already know
until you explicitly confirm it. This is not specific to this tap — every
third-party tap (anyone's, not just this one) requires the same step
before `brew install` will do anything with it. The confirmation is stored
locally (`~/.homebrew/trust.json` or under `$XDG_CONFIG_HOME`), keyed by
tap name — it's a one-time thing **per machine**, not per install: verified
that uninstalling and untapping does *not* clear it, so re-tapping later on
the same machine does not require running `brew trust` again. A different
machine (or a fresh Homebrew install) starts with nothing trusted and does
need the step once.

**3. `brew install siriokds/dual-uade/dual-uade`**
Runs the formula: downloads `mvtiaine/uade` (branch `dragnet`) plus its two
small build-time dependencies (`libzakalwe`, `bencode-tools`), then
`./configure && make install` for all three, in
`/opt/homebrew/Cellar/dual-uade/<version>/`. Takes a few seconds — these
are small C codebases, nothing heavy is compiled.

## Where things end up, and why `keg_only`

The formula is marked `keg_only`, which means Homebrew does **not** create
its usual symlinks into `/opt/homebrew/lib`, `/opt/homebrew/include`, etc.
This is deliberate: the official `uade` formula (stock UADE) already
provides a `libuade.dylib` under those exact same symlinked names, and
`dual-uade` is meant to coexist with it, not replace it — both can be
installed on the same machine at once. To find what got built:

```sh
/opt/homebrew/opt/dual-uade/lib/libuade.dylib     # the library itself
/opt/homebrew/opt/dual-uade/include/uade/uade.h   # the public C API (identical to stock UADE's)
/opt/homebrew/opt/dual-uade/bin/uade123           # a standalone CLI player, useful for a quick test
```

A caller (Dual, or anything else) that wants to use this build specifically
instead of the stock one must point at that `/opt/dual-uade/...` path
explicitly — `keg_only` means it will never be picked up "by accident"
just by looking in the usual shared locations.

## Verifying the install actually built the fork (not stock UADE)

Stock UADE doesn't support Face The Music; this fork does. A quick sanity
check after installing:

```sh
grep -c FaceTheMusic /opt/homebrew/opt/dual-uade/share/uade/eagleplayer.conf
# → 1 if this is really the fork; 0 would mean something went wrong
```

## Uninstalling / re-tapping

```sh
brew uninstall dual-uade
brew untap siriokds/dual-uade
```

`brew untap` will refuse to run while the formula is still installed —
uninstall first, as above.

License: GNU General Public License v2.0, inherited from UADE — see the
[dual-uade](https://github.com/siriokds/dual-uade) repository for the
source and the full license text.
