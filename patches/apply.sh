#!/bin/sh
# Apply patches to the working tree, tracking what's applied in
# .patches-applied (gitignored). Idempotent. Rebuild afterwards with `make`.
#
#   ./patches/apply.sh             apply every not-yet-applied patch
#   ./patches/apply.sh <name>...   apply just those (exact name or a unique
#                                  prefix, e.g. 06, undotree, 02-quitall)
#
# Patches are applied in the order given; dependent patches must come after
# the ones they build on (apply.sh with no arguments applies them all in
# numeric order).

cd "$(dirname "$0")/.." || exit 1

MARK=.patches-applied
[ -f "$MARK" ] || : > "$MARK"

applied=0; skipped=0; failed=0

apply_one() {
    p="$1"
    b=$(basename "$p")
    if grep -qx "$b" "$MARK"; then
        echo "already applied: $p"
        skipped=$((skipped + 1))
        return
    fi
    if git apply "$p" 2>/dev/null; then
        echo "applied: $p"
        echo "$b" >> "$MARK"
        applied=$((applied + 1))
    else
        echo "CONFLICT (does not apply cleanly): $p"
        failed=$((failed + 1))
    fi
}

if [ "$#" -eq 0 ]; then
    for p in patches/*.diff; do
        [ -e "$p" ] || continue
        apply_one "$p"
    done
else
    for name in "$@"; do
        p="patches/$name.diff"
        [ -f "$p" ] || p=$(ls patches/*"$name"*.diff 2>/dev/null | head -1)
        if [ -f "$p" ]; then
            apply_one "$p"
        else
            echo "no such patch: $name"
            failed=$((failed + 1))
        fi
    done
fi

echo "applied=$applied already=$skipped failed=$failed"
# Drop stale build artifacts so `make` always rebuilds patched sources.
if [ "$applied" -gt 0 ]; then
    rm -f *.o modif
fi
[ "$failed" -eq 0 ]