#!/bin/sh
# Remove one applied patch by name (without the .diff suffix).
# usage: patches/remove.sh <name>

name="$1"
[ -n "$name" ] || { echo "usage: $0 <name>"; exit 1; }
cd "$(dirname "$0")/.." || exit 1
p="patches/$name.diff"
[ -f "$p" ] || { echo "no such patch: $p"; exit 1; }
git apply --reverse "$p" && echo "removed: $p"