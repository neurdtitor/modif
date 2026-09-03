#!/bin/sh
# Apply every patch in patches/ that is not already applied (tracked in
# .patches-applied). Idempotent. Rebuild afterwards with `make`.
# usage: patches/apply.sh

cd "$(dirname "$0")/.." || exit 1

MARK=.patches-applied
[ -f "$MARK" ] || : > "$MARK"

applied=0; skipped=0; failed=0
for p in patches/*.diff; do
    [ -e "$p" ] || continue
    b=$(basename "$p")
    if grep -qx "$b" "$MARK"; then
        echo "already applied: $p"
        skipped=$((skipped + 1))
        continue
    fi
    if git apply "$p" 2>/dev/null; then
        echo "applied: $p"
        echo "$b" >> "$MARK"
        applied=$((applied + 1))
    else
        echo "CONFLICT (does not apply cleanly): $p"
        failed=$((failed + 1))
    fi
done
echo "applied=$applied already=$skipped failed=$failed"
# Drop stale build artifacts so `make` always rebuilds patched sources.
if [ "$applied" -gt 0 ]; then
    rm -f *.o modif
fi
[ "$failed" -eq 0 ]