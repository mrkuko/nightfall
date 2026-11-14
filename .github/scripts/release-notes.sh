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
    for t in $(git tag --list 'v[0-9]*' --sort=-creatordate); do      # version sort gets suffixes wrong
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

# commit subjects since PREV (two messages typed into one subject are split; the version bump
# commits "chore: release vX.Y.Z" / "chore: bump version ..." are left out)
LOG=$(git log --no-merges --pretty='%s' "$RANGE" |
      sed 's/ \(feat\|fix\|docs\|build\|ci\|chore\|refactor\|perf\|test\|style\)\([(:]\)/\n\1\2/g' |
      sed 's/[;,] *$//' |
      grep -vE '^chore(\([^)]*\))?: (release|bump version)' || true)

# One section. Grouping: first the commits without a scope together with the scopes that occur only
# once in this section ("- **Scope:** text"); then every scope that occurs two or more times becomes
# one bold bullet with its commits as a nested list, in the order the scopes first appear.
section() {   # title, regex of types
    out=$(printf '%s\n' "$LOG" | sed -n -E "s/^($2)(\\(([^)]*)\\))?!?: (.+)\$/\\3\\t\\4/p" |
          awk -F'\t' '
            function cap(s) { return toupper(substr(s, 1, 1)) substr(s, 2) }
            { n++; sc[n] = $1; tx[n] = $2
              if ($1 != "") { if (!($1 in cnt)) order[++no] = $1; cnt[$1]++ } }
            END {
              for (i = 1; i <= n; i++)
                if (sc[i] == "") printf "- %s\n", tx[i]
                else if (cnt[sc[i]] < 2) printf "- **%s:** %s\n", cap(sc[i]), tx[i]
              for (k = 1; k <= no; k++) {
                s = order[k]
                if (cnt[s] < 2) continue
                printf "- **%s**\n", cap(s)
                for (i = 1; i <= n; i++) if (sc[i] == s) printf "  - %s\n", tx[i]
              }
            }')
    [ -n "$out" ] && printf '### %s\n\n%s\n\n' "$1" "$out"
    return 0
}
section "New" "feat"
section "Fixes" "fix"
section "Performance" "perf"
section "Documentation" "docs"
section "Build and packaging" "build|ci"
section "Maintenance" "chore|refactor|test|style"
other=$(printf '%s\n' "$LOG" | grep -vE '^(feat|fix|perf|docs|build|ci|chore|refactor|test|style)(\(|:|!)' | grep -v '^$' | sed 's/^/- /' || true)
[ -n "$other" ] && printf '### Other\n\n%s\n\n' "$other"

if [ -n "$PREV" ]; then
    echo "**Full changelog:** https://github.com/$REPO/compare/$PREV...$TAG"
else
    echo "First release."
fi
