# Patches

Features are distributed as `patches/<name>.diff`, one feature per file —
the suckless way. The base tree (`HEAD`) stays minimal and pristine; each
patch layers one optional feature on top.

## Conventions

- One feature per `.diff`, named after it: `leader.diff`, `linenumbers.diff`.
- A patch is generated with `git diff HEAD` against a **clean** tree, so it
  contains exactly one feature and nothing else.
- Applied patches are **never committed**: `HEAD` is always the pristine core.
  (Otherwise patches would start overlapping and the whole model breaks.)
- Patches are kept small and non-overlapping, so any subset applies cleanly.
- If the base changes under a patch, regenerate it: remove, edit, re-`mkpatch`.

## Workflow

### Apply everything and rebuild

    ./patches/apply.sh && make

Idempotent: patches that are already applied are skipped, so running it
again is safe. Patches that no longer apply cleanly are reported as conflicts.

### Remove one patch

    ./patches/remove.sh <name>

`<name>` is without the `.diff` suffix, e.g. `./patches/remove.sh leader`.

### Create a new patch

    1. Start clean:      git checkout .        # discards edits and applied patches
    2. Hack the source or config.h.
    3. Capture:          ./patches/mkpatch.sh <name>
    4. Discard the edit: git checkout .
    5. Apply + build:    ./patches/apply.sh && make

`mkpatch.sh` refuses to run while other patches are applied, so the diff you
capture is always just your new feature. If you have uncommitted work you
want to keep, `git stash` it before starting step 1.

## Scripts

| Script                 | Purpose                                   |
|------------------------|-------------------------------------------|
| `patches/apply.sh`     | apply all `.diff` files (idempotent)      |
| `patches/remove.sh`    | un-apply one patch                        |
| `patches/mkpatch.sh`   | capture current edits as a new patch      |

Patches are plain `git diff` output and also work with stock tooling:
`git apply patches/<name>.diff` or `patch -p1 < patches/<name>.diff`.