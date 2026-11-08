#!/bin/sh
# release-notes.sh — writes the release notes (markdown) to stdout.
#
#   sh .github/scripts/release-notes.sh TAG [PREV_TAG] [END_REF]
#
# Versioning: vX.Y.0 is a full release, vX.Y.Z with Z > 0 is a nightly build.
# A full release lists everything since the previous full release (so it includes the nightlies
# in between); a nightly lists the changes since the previous tag. The changes are the commit
# subjects grouped by conventional-commit type (feat, fix, docs, ...).
set -eu

TAG=$1
END=${3:-$TAG}
VER=${TAG#v}
PATCH=${VER##*.}
REPO=${GITHUB_REPOSITORY:-mrkuko/nightfall}

if [ "$PATCH" = 0 ]; then KIND=full; else KIND=nightly; fi

# previous tag: for a full release the previous full release, for a nightly any previous tag
PREV=${2:-}
if [ -z "$PREV" ]; then
    for t in $(git tag --list 'v[0-9]*' --sort=-v:refname); do
        [ "$t" = "$TAG" ] && continue
        git merge-base --is-ancestor "$t" "$END" 2>/dev/null || continue
        if [ "$KIND" = full ]; then
            case $t in *.0) PREV=$t; break ;; esac
        else
            PREV=$t; break
        fi
    done
fi
RANGE=${PREV:+$PREV..}$END

echo "A living procedural night forest in your terminal."
echo
if [ "$KIND" = nightly ]; then
    echo "> **Nightly build.** Fresh from development, it may be rough around the edges."
    echo "> Full releases are the \`vX.Y.0\` ones."
    echo
fi

cat <<E2
## Downloads

| System | File | Install |
|---|---|---|
| Debian, Ubuntu | \`nightfall_${VER}_amd64.deb\` / \`arm64.deb\` | \`sudo apt install ./nightfall_*.deb\` |
| Fedora | \`nightfall-${VER}-1.x86_64.rpm\` / \`aarch64.rpm\` | \`sudo dnf install ./nightfall-*.rpm\` |
| Arch Linux | \`nightfall-${VER}-1-x86_64.pkg.tar.zst\` | \`sudo pacman -U nightfall-*.pkg.tar.zst\` |
| any Linux | \`nightfall-${VER}-linux-x86_64.tar.gz\` / \`aarch64\` | unpack and run \`nightfall\` |
| macOS | \`nightfall-${VER}-macos-universal.tar.gz\` | unpack and run \`nightfall\` |
| Windows | \`nightfall-${VER}-windows-x86_64.zip\` | unpack and run \`nightfall.exe\` in Windows Terminal |

Checksums are in \`SHA256SUMS\`.

## What's new

E2

# commit subjects since PREV, grouped by type
LOG=$(git log --no-merges --pretty='%s' "$RANGE" | sed 's/ \(feat\|fix\|docs\|build\|ci\|chore\|refactor\|perf\|test\)\([(:]\)/\n\1\2/g')
section() {   # title, regex of types
    out=$(printf '%s\n' "$LOG" | sed -n -E "s/^($2)(\\(([^)]*)\\))?!?: (.+)\$/\\3\\t\\4/p" |
          awk -F'\t' '{ if ($1 != "") printf "- **%s:** %s\n", $1, $2; else printf "- %s\n", $2 }')
    [ -n "$out" ] && printf '### %s\n\n%s\n\n' "$1" "$out"
    return 0
}
section "New" "feat"
section "Fixes" "fix"
section "Performance" "perf"
section "Documentation" "docs"
section "Build and packaging" "build|ci"
section "Maintenance" "chore|refactor|test"
other=$(printf '%s\n' "$LOG" | grep -vE '^(feat|fix|perf|docs|build|ci|chore|refactor|test)(\(|:|!)' | grep -v '^$' | sed 's/^/- /' || true)
[ -n "$other" ] && printf '### Other\n\n%s\n\n' "$other"

if [ -n "$PREV" ]; then
    echo "**Full changelog:** https://github.com/$REPO/compare/$PREV...$TAG"
else
    echo "First release."
fi
