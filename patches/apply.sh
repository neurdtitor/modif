#!/bin/sh
# Apply every patch in patches/ that is not already applied.
# Idempotent: safe to run repeatedly. Rebuild afterwards with `make`.
# usage: patches/apply.sh

cd "$(dirname "$0")/.." || exit 1

applied=0; skipped=0; failed=0
for p in patches/*.diff; do
    [ -e "$p" ] || continue
    if git apply --reverse --check "$p" 2>/dev/null; then
        echo "already applied: $p"
        skipped=$((skipped + 1))
    elif git apply --check "$p" 2>/dev/null; then
        git apply "$p" && { echo "applied: $p"; applied=$((applied + 1)); } \
            || { echo "FAILED: $p"; failed=$((failed + 1)); }
    else
        echo "CONFLICT (does not apply cleanly): $p"
        failed=$((failed + 1))
    fi
done
echo "applied=$applied already=$skipped failed=$failed"
[ "$failed" -eq 0 ]