#!/bin/sh
# build-packages.sh — builds the .deb, .rpm and Arch packages from ./nightfall.
#
#   sh packaging/build-packages.sh [VERSION] [ARCH] [OUTDIR]
#       VERSION  x.y.z (default: from the Makefile)     ARCH  amd64 | arm64 (default: this machine)
#       OUTDIR   where the packages go (default: dist)
#
# The manual page docs/nightfall.6 is compressed into a temporary file and added to every package
# automatically (as /usr/share/man/man6/nightfall.6.gz), the source tree is left untouched.
# Uses `nfpm` if it is installed, otherwise its official Docker image.
set -eu
cd "$(dirname "$0")/.."

VERSION=${1:-$(sed -n 's/^VERSION[ ?]*[:?]*= *//p' Makefile | head -1)}
case ${2:-$(uname -m)} in
    amd64|x86_64)  NF_ARCH=amd64 ;;
    arm64|aarch64) NF_ARCH=arm64 ;;
    *) echo "unsupported architecture: ${2:-$(uname -m)}" >&2; exit 1 ;;
esac
OUT=${3:-dist}
[ -x nightfall ] || { echo "build ./nightfall first (make)" >&2; exit 1; }
[ -f docs/nightfall.6 ] || { echo "docs/nightfall.6 is missing" >&2; exit 1; }

mkdir -p "$OUT" build
sed "s/\"nightfall [0-9][0-9.]*\"/\"nightfall $VERSION\"/" docs/nightfall.6 | gzip -9nc > build/nightfall.6.gz
export NF_ARCH NF_VERSION="$VERSION"

for fmt in deb rpm archlinux; do
    if command -v nfpm >/dev/null 2>&1; then
        nfpm package --config packaging/nfpm.yaml --packager "$fmt" --target "$OUT/"
    elif command -v docker >/dev/null 2>&1; then
        docker run --rm -v "$PWD:/w" -w /w -e NF_ARCH -e NF_VERSION \
            goreleaser/nfpm:v2.48.0 package --config packaging/nfpm.yaml --packager "$fmt" --target "$OUT/"
    else
        echo "need nfpm or docker (https://nfpm.goreleaser.com)" >&2
        exit 1
    fi
done
ls -l "$OUT"
