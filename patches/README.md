# Patches

Features are distributed as `patches/<name>.diff`, one feature per file —
the suckless way. The base tree (`HEAD`) stays minimal and pristine; each
patch layers one optional feature on top.

## Conventions

- One feature per `.diff`, named after it: `01-buffers.diff`, `02-quitall.diff`.
- The two-digit prefix fixes the **apply order**: a patch may build on an
  earlier one (`02-quitall` needs `01-buffers`), so patches must apply in
  numeric order. `apply.sh` applies them in that order.
- A patch is generated with `git diff HEAD` against a clean tree, so it
  contains exactly one feature and nothing else. Build artifacts (`.o`,
  the binary) are never included.
- Applied patches are **never committed**: `HEAD` is always the pristine core.
  (Otherwise patches would start overlapping and the whole model breaks.)
- If the base changes under a patch, regenerate it: remove, edit, re-`mkpatch`.

## Workflow

### Apply everything and rebuild

    ./patches/apply.sh && make

Idempotent: `apply.sh` records what it has applied in `.patches-applied`
(gitignored) and skips those on later runs. It removes the `.o` files and the
binary after applying, so `make` always rebuilds from the patched sources —
this also sidesteps stale-builds caused by sub-second mtimes. Patches that no
longer apply cleanly are reported as conflicts.

### Remove one patch

    ./patches/remove.sh <name>

`<name>` is a prefix, so `./patches/remove.sh quitall` or
`./patches/remove.sh 02-quitall` both work. Removing a patch that others
build on breaks those — remove them in reverse order.

### Create a new patch

    1. Start clean:      git checkout .        # discards edits and applied patches
    2. Hack the source or config.h.
    3. Capture:          ./patches/mkpatch.sh <name>
    4. Discard the edit: git checkout .
    5. Apply + build:    ./patches/apply.sh && make

`git diff HEAD` captures only your new work, so make sure the working tree
differs from `HEAD` only by this feature (commit or `git checkout .` any
other changes first). If you have uncommitted work you want to keep, `git
stash` it before starting step 1. For a patch that builds on applied ones,
apply them first, hack on top, and number the new patch after the last one.

## Scripts

| Script                 | Purpose                                   |
|------------------------|-------------------------------------------|
| `patches/apply.sh`     | apply all `.diff` files (idempotent)      |
| `patches/remove.sh`    | un-apply one patch                        |
| `patches/mkpatch.sh`   | capture current edits as a new patch      |

Patches are plain `git diff` output and also work with stock tooling:
`git apply patches/<name>.diff` or `patch -p1 < patches/<name>.diff`.