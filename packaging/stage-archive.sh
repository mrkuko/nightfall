#!/bin/sh
# stage-archive.sh — lays out the files of a release archive (tar.gz / zip) in a directory.
#
#   [NO_FORMAT=1] [EXTRA="file ..."] sh packaging/stage-archive.sh DIR BINARY [man]
#
# Copies the binary, LICENSE, FORMAT.md (not with NO_FORMAT=1: the Windows zip; the README then links to it on GitHub) (and with `man` the man page, and the files listed in $EXTRA), puts the images that the
# README shows into DIR/Images/ and writes a README.md whose links are rewritten for the archive:
#   docs/images/X          ->  ./Images/X
#   assets/FORMAT.md       ->  FORMAT.md
#   tools/sketch_rig.py    ->  the file on GitHub (it is not shipped)
set -eu
cd "$(dirname "$0")/.."
dest=$1
bin=$2
withman=${3:-}
repo=${GITHUB_REPOSITORY:-mrkuko/nightfall}

mkdir -p "$dest/Images"
cp "$bin" LICENSE "$dest/"
fmt='(FORMAT.md)'
if [ -n "${NO_FORMAT:-}" ]; then fmt="(https://github.com/$repo/blob/main/assets/FORMAT.md)"; else cp assets/FORMAT.md "$dest/"; fi
[ "$withman" = man ] && cp docs/nightfall.6 "$dest/"
for f in ${EXTRA:-}; do cp "$f" "$dest/"; done

# only the images the README actually uses (not the icon source etc.)
for f in $(grep -oE 'docs/images/[A-Za-z0-9._-]+' README.md | sort -u); do
    cp "$f" "$dest/Images/"
done

sed -E \
    -e 's#docs/images/#./Images/#g' \
    -e "s#\\(assets/FORMAT\\.md\\)#$fmt#g" \
    -e "s#\\(tools/sketch_rig\\.py\\)#(https://github.com/$repo/blob/main/tools/sketch_rig.py)#g" \
    README.md > "$dest/README.md"

# check: every relative link and image in the staged README points to a file that exists
missing=0
for p in $(grep -oE '(\]\(|src=")[^)" ]+' "$dest/README.md" | sed -E 's/^(\]\(|src=")//' | grep -v '^https\?:' | grep -v '^#'); do
    [ -e "$dest/$p" ] || { echo "stage-archive: broken link in README: $p" >&2; missing=1; }
done
[ "$missing" = 0 ] || exit 1
