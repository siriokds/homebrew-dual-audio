# homebrew-dual-uade

Homebrew tap for [dual-uade](https://github.com/siriokds/dual-uade) —
UADE (`mvtiaine/uade`, branch `dragnet`) built as a drop-in `libuade.dylib`
for [Dual](https://github.com/siriokds/dual), with extra Amiga format
support beyond stock UADE.

```sh
brew tap siriokds/dual-uade https://github.com/siriokds/homebrew-dual-uade
brew trust siriokds/dual-uade --tap
brew install siriokds/dual-uade/dual-uade
```

`brew trust` is required because this is a third-party tap. The formula is
`keg_only`: it installs alongside the official `uade` formula without
conflict, since both provide a `libuade.dylib` under the same name.

License: GNU General Public License v2.0, inherited from UADE — see the
[dual-uade](https://github.com/siriokds/dual-uade) repository for the
source and the full license text.
