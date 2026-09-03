#!/bin/sh
# Capture the current uncommitted changes as a new patch.
# Start from a clean tree (git checkout . or git stash), hack, then run this.
# Refuses to run while other patches are applied so the diff stays isolated.
# usage: patches/mkpatch.sh <name>

name="$1"
[ -n "$name" ] || { echo "usage: $0 <name>"; exit 1; }
cd "$(dirname "$0")/.." || exit 1

for p in patches/*.diff; do
    [ -e "$p" ] || continue
    if git apply --reverse --check "$p" 2>/dev/null; then
        echo "refusing: $p is currently applied; remove it first (git checkout . or patches/remove.sh)"
        exit 1
    fi
done

out="patches/$name.diff"
git diff HEAD > "$out"
if [ -s "$out" ]; then
    echo "wrote $out ($(wc -l < "$out") lines)"
else
    rm -f "$out"
    echo "no changes to capture"
    exit 1
fi