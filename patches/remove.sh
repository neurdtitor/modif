#!/bin/sh
# Remove one applied patch by name or by a unique prefix of it.
# usage: patches/remove.sh <name>

name="$1"
[ -n "$name" ] || { echo "usage: $0 <name>"; exit 1; }
cd "$(dirname "$0")/.." || exit 1

MARK=.patches-applied
p="patches/$name.diff"
[ -f "$p" ] || p=$(ls patches/*"$name"*.diff 2>/dev/null | head -1)
[ -f "$p" ] || { echo "no such patch: $name"; exit 1; }
b=$(basename "$p")
if git apply --reverse "$p" 2>/dev/null; then
    echo "removed: $p"
    rm -f *.o modif
    if [ -f "$MARK" ]; then
        grep -vx "$b" "$MARK" > "$MARK.tmp" && mv "$MARK.tmp" "$MARK"
    fi
else
    echo "cannot reverse $p (not applied cleanly?)"
    exit 1
fi