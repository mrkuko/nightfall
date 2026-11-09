#!/bin/sh
# sync-branches.sh — merges origin/main into every other branch on origin that is behind it.
#
#   sh .github/scripts/sync-branches.sh [MAIN_BRANCH]
#
# A branch that merges cleanly is pushed. A branch with conflicts (or a rejected push) is left
# alone and reported, and the script exits with 1 so that the failed run notifies you.
set -u
MAIN=${1:-main}

git config user.name >/dev/null 2>&1 || git config user.name "github-actions[bot]"
git config user.email >/dev/null 2>&1 || git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
git fetch -q origin --prune

failed=""
for ref in $(git for-each-ref --format='%(refname:short)' refs/remotes/origin/); do
    br=${ref#origin/}
    case $br in HEAD|origin|"$MAIN") continue ;; esac
    if git merge-base --is-ancestor "origin/$MAIN" "origin/$br"; then
        echo "$br: already contains $MAIN"
        continue
    fi
    git checkout -q -B "$br" "origin/$br"
    if git merge --no-edit -m "chore: merge $MAIN into $br" "origin/$MAIN" >/dev/null 2>&1; then
        if git push -q origin "$br"; then
            echo "$br: merged $MAIN"
        else
            echo "::warning::$br: merged locally but the push was rejected"
            failed="$failed $br"
        fi
    else
        git merge --abort
        echo "::warning::$br: conflicts with $MAIN, merge it by hand"
        failed="$failed $br"
    fi
done
git checkout -q "$MAIN" 2>/dev/null || git checkout -q -B "$MAIN" "origin/$MAIN"

if [ -n "$failed" ]; then
    echo "::error::could not sync:$failed"
    exit 1
fi
