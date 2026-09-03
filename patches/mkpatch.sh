#!/bin/sh
# Capture the current uncommitted changes as a new patch.
# The working tree should differ from HEAD only by this new feature
# (commit or remove other work first so `git diff HEAD` stays clean).
# usage: patches/mkpatch.sh <name>

name="$1"
[ -n "$name" ] || { echo "usage: $0 <name>"; exit 1; }
cd "$(dirname "$0")/.." || exit 1

out="patches/$name.diff"
git diff HEAD -- . ':(exclude)*.o' ':(exclude)modif' > "$out"
if [ -s "$out" ]; then
    echo "wrote $out ($(wc -l < "$out") lines)"
else
    rm -f "$out"
    echo "no changes to capture"
    exit 1
fi