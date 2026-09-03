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
        └─ 01-buffers.diff       :new/:e/:bd, quit-all prompts, buffer fuzzy
             └─ 02-multicursor.diff   multiple cursors (builds on the base's
             │                        buffer list and per-buffer view state)
                  └─ 03-treesitter.diff   tree-sitter syntax highlighting

  The buffer machinery that used to live in `01` (the `BufList`, per-buffer
  view state, `buf_sync`/`buf_load`, undo reset on switch, `:bn`/`:bp`/`:ls`)
  is **core now**, so `01` is a thin layer of commands and prompts. `02` and
  `03` depend on the base's buffer support rather than on `01`'s internals.
- A patch is generated with `git diff HEAD` against a clean tree, so it
  contains exactly one feature and nothing else. Build artifacts (`.o`,
  the binary) are never included.
- Applied patches are **never committed**: `HEAD` is always the pristine core.
  (Otherwise patches would start overlapping and the whole model breaks.)
- If the base changes under a patch, regenerate it: remove, edit, re-`mkpatch`.

### Buffer state is a base invariant

The base owns buffer lifecycle: `Buffer` carries per-buffer view state
(`cur`/`top`/`left`/`mark`/`dirty`), `buffer.c` owns the `BufList`, and
`edit.c`'s `buf_sync`/`buf_load`/`buf_add`/`buf_switch` fold the active
buffer's content, view, and gap metadata back into its slot on switch. The
active copy and `bl->bufs[bl->cur]` **share storage** and must always be
synced together — a patch must never copy only part of a `Buffer` struct, or
edits will silently vanish on switch. The undo log is global, linear, and
reset by the base on every buffer switch; no patch may redefine undo's shape.

### Undo is frozen

Undo is **linear, in the base, and never touched by a patch**. The base's
`cmd_undo`/`cmd_redo` in `edit.c` are the one and only undo model; no patch
may redefine undo's shape (not per-buffer, not a tree). The base resets the
global undo log on buffer switch, so undo never grows stale across buffers.
If a tree/branching undo is ever wanted, it is the one patch allowed to
rewrite undo — and once merged, nothing else redefines it again.

### One struct per feature

A patch that adds state adds a **struct owned by its own file**, and the
Editor only holds a pointer to it — it does not bolt fields onto `Editor`.
`02-multicursor` adds `MCState` in `mc.c`; `03-treesitter` adds `HLState` in
`highlight.c`. (`01-buffers` needs no struct: the buffer list it used to
carry is base.) This keeps patches isolated and reviewable.

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
stash` it before starting step 1.

Note: `git checkout .` also restores tracked patch files, so when you
**regenerate an existing patch** (not create a new one), keep the new
`.diff`: `git checkout -- <source files>` instead of `git checkout .`. For a
patch that builds on applied ones, apply them first, hack on top, and number
the new patch after the last one — then capture it against a temporary commit
of the applied state:

    git add -A && git commit -m temp            # base + applied patches
    git reset --hard HEAD~1                      # back to the base
    <re-apply the patches>
    git add -A && git commit -m temp2            # base + patches again
    git diff temp2 temp -- . ':(exclude)patches/*.diff' > patches/<name>.diff

## Scripts

| Script                 | Purpose                                   |
|------------------------|-------------------------------------------|
| `patches/apply.sh`     | apply all patches, or named ones          |
| `patches/remove.sh`    | un-apply one patch, or all                |
| `patches/mkpatch.sh`   | capture current edits as a new patch      |

Patches are plain `git diff` output and also work with stock tooling:
`git apply patches/<name>.diff` or `patch -p1 < patches/<name>.diff`.