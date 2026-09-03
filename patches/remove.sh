#!/bin/sh
# Remove applied patches from the working tree.
#
#   ./patches/remove.sh <name>   remove one patch (exact name or a unique
#                                prefix, e.g. 06, undotree, 02-quitall)
#   ./patches/remove.sh all      remove every applied patch (reverse order)
#
# Patches that build on the removed one will stop applying; remove dependent
# patches first (`all` does this for you).

cd "$(dirname "$0")/.." || exit 1

MARK=.patches-applied

removed=0; failed=0

remove_one() {
    p="$1"
    b=$(basename "$p")
    if [ ! -f "$p" ]; then
        echo "missing file: $p"
        failed=$((failed + 1))
        return
    fi
    if git apply --reverse "$p" 2>/dev/null; then
        echo "removed: $p"
        removed=$((removed + 1))
        if [ -f "$MARK" ]; then
            grep -vx "$b" "$MARK" > "$MARK.tmp" 2>/dev/null; mv "$MARK.tmp" "$MARK"
        fi
    else
        echo "cannot reverse $p (not applied cleanly?)"
        failed=$((failed + 1))
    fi
}

case "$1" in
    all|-a)
        for b in $(grep -v '^$' "$MARK" 2>/dev/null | sort -r); do
            remove_one "patches/$b"
        done
        ;;
    *)
        name="$1"
        [ -n "$name" ] || { echo "usage: $0 <name> | all"; exit 1; }
        p="patches/$name.diff"
        [ -f "$p" ] || p=$(ls patches/*"$name"*.diff 2>/dev/null | head -1)
        [ -f "$p" ] || { echo "no such patch: $name"; exit 1; }
        remove_one "$p"
        ;;
esac

echo "removed=$removed failed=$failed"
if [ "$removed" -gt 0 ]; then
    rm -f *.o modif
fi
[ "$failed" -eq 0 ]