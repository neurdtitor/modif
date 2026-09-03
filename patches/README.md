# Patches

Features are distributed as `patches/<name>.diff`, one feature per file —
the suckless way. The base tree (`HEAD`) stays minimal and pristine; each
patch layers one optional feature on top.

## Conventions

- One feature per `.diff`, named after it: `01-buffers.diff`, `02-multicursor.diff`.
- The two-digit prefix fixes the **apply order**: a patch may build on an
  earlier one, so patches must apply in numeric order. `apply.sh` applies them
  in that order. The current chain is:

      HEAD (pristine base)
        └─ 01-buffers.diff       multi-buffer + quit-all + buffer fuzzy
             └─ 02-multicursor.diff   multiple cursors (builds on 01)
                  └─ 03-treesitter.diff   tree-sitter syntax highlighting (builds on 01)

  Each patch depends on the one before it: 02 needs 01's `E.bl` buffer list,
  03 needs 01's per-buffer `hl_dirty()` reset. Do not reorder them.
- A patch is generated with `git diff HEAD` against a clean tree, so it
  contains exactly one feature and nothing else. Build artifacts (`.o`,
  the binary) are never included.
- Applied patches are **never committed**: `HEAD` is always the pristine core.
  (Otherwise patches would start overlapping and the whole model breaks.)
- If the base changes under a patch, regenerate it: remove, edit, re-`mkpatch`.

### Undo is frozen

Undo is **linear, in the base, and never touched by a patch**. The base's
`cmd_undo`/`cmd_redo` in `edit.c` are the one and only undo model; no patch
may redefine undo's shape (not per-buffer, not a tree). `01-buffers` resets
the global undo log on buffer switch, but the model itself stays the base's.
If a tree/branching undo is ever wanted, it is the one patch allowed to
rewrite undo — and once merged, nothing else redefines it again.

### One struct per feature

A patch that adds state adds a **struct owned by its own file**, and the
Editor only holds a pointer to it — it does not bolt fields onto `Editor`.
`02-multicursor` adds `MCState` in `mc.c`; `03-treesitter` adds `HLState` in
`highlight.c`. This keeps patches isolated and reviewable.

## Workflow

### Apply patches and rebuild

    ./patches/apply.sh && make

With no arguments `apply.sh` applies every patch that isn't applied yet,
in numeric order. To apply just one (or a few), name it:

    ./patches/apply.sh buffers
    ./patches/apply.sh 02 multicursor        # names / unique prefixes

Idempotent: `apply.sh` records what it has applied in `.patches-applied`
(gitignored) and skips those on later runs. It removes the `.o` files and the
binary after applying, so `make` always rebuilds from the patched sources —
this also sidesteps stale-builds caused by sub-second mtimes. Patches that no
longer apply cleanly are reported as conflicts.

### Remove patches

    ./patches/remove.sh <name>    # one patch
    ./patches/remove.sh all       # every applied patch (reverse order)

`<name>` is a prefix, so `./patches/remove.sh multicursor` or
`./patches/remove.sh 02-multicursor` both work. Removing a patch that others
build on breaks those — remove them in reverse order (`all` does this for
you).

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
| `patches/apply.sh`     | apply all patches, or named ones          |
| `patches/remove.sh`    | un-apply one patch, or all                |
| `patches/mkpatch.sh`   | capture current edits as a new patch      |

Patches are plain `git diff` output and also work with stock tooling:
`git apply patches/<name>.diff` or `patch -p1 < patches/<name>.diff`.