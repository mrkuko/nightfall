# Packaging

`nfpm.yaml` describes one package that is built three ways (nfpm, run by CI in its official
Docker image): `.deb` (Debian, Ubuntu), `.rpm` (Fedora, openSUSE) and `.pkg.tar.zst` (Arch),
for x86_64 and aarch64. The packages contain the static binary from the release build, so they
have no dependencies and install offline.

`sh packaging/build-packages.sh [VERSION] [ARCH] [OUTDIR]` builds all three from `./nightfall`; it also
gzips `docs/nightfall.6` and adds it to each package (installed as `man nightfall`). It needs
[nfpm](https://nfpm.goreleaser.com) or Docker. CI runs the same script.

Versioning: a tag `vX.Y.0` makes a full release (created as a draft with the notes to review, then
published by hand), a tag `vX.Y.Z` with `Z` above 0 makes a nightly build (published at once as a
pre-release). The notes are written by `.github/scripts/release-notes.sh` from the commit messages
(`feat:`, `fix:`, `docs:` ...). Releasing: `git tag v1.0.0 && git push origin v1.0.0`.

The release archives (tar.gz, zip) are laid out by `packaging/stage-archive.sh`: besides the binary they
contain the README with its images in `Images/` (the links are rewritten from `docs/images/` to
`./Images/`), the license and FORMAT.md.

CI builds them on every push (`ci.yml`, artifact `packages`) and attaches them to a GitHub
release when a tag is pushed (`release.yml`): `git tag v1.0.0 && git push origin v1.0.0`.

Installing a downloaded package:

```sh
sudo apt install ./nightfall_*_amd64.deb            # Debian, Ubuntu
sudo dnf install ./nightfall-*.x86_64.rpm           # Fedora
sudo pacman -U nightfall-*-x86_64.pkg.tar.zst       # Arch
```

For any other system `make install` works (`PREFIX`, `DESTDIR`).
