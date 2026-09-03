# modif — a minimal, hackable terminal text editor

A small, readable C text editor built to be **patched, not plugged into**.
No runtime scripting language, no plugin API — you hack the source directly
and recompile. If you can't read the whole core in an evening, it's too big.

## Philosophy

- **Small core.** Every file has one job. No file should require you to
  understand three other files first.
- **Patches over plugins.** Extensibility comes from editing `config.h` and
  the source itself, not from a runtime API. Distribute changes as diffs.
- **Config as data, behavior as code.** `config.h` holds keybindings and
  constants as plain arrays/structs you edit and recompile — no separate
  config language.
- **Zero unnecessary dependencies.** libc and a terminal. That's it, for now.
- **Do a few things well.** Editing, running code, and finding files — not
  everything.

## Status

v0 works: gap-buffer editing, modal insert/normal/visual, `:w`/`:q`
commands, search, fuzzy finder, an embedded terminal pane, and a system
clipboard. Features that aren't core — multiple buffers, multiple cursors,
tree-sitter syntax highlighting — ship as ordered patches (see below). The
core stays deliberately small.

## What's here (v0)

- **Core editing**: gap buffer, insert/normal/visual modes, motions and
  editing commands, linear undo/redo, yank/paste
- **`config.h`-based keybindings**: a plain array of `{key, function}`
  entries — no keybinding DSL
- **File I/O**: open, save, dirty-buffer warning on quit
- **Terminal UI**: raw mode + direct ANSI escape sequences, no external
  TUI library
- **Split panes**: minimal split hosting an embedded terminal beside a
  buffer
- **Embedded terminal**: a real PTY (`forkpty()`) inside a split, with a
  minimal VT100/ANSI parser (subset inspired by `st`) — compile and run
  code without leaving the editor
- **Fuzzy finder**: built-in file picker with subsequence matching and
  fzf-style scoring, launched as a popup
- **System clipboard**: `pbcopy`/`pbpaste` (macOS) or `wl-copy`/`xclip`/
  `xsel` elsewhere, bound to `<Space>Y` / `<Space>P`
- **Patch-friendly layout**: `buffer.c`, `terminal.c`, `input.c`, `pty.c`,
  `fuzzy.c`, `clipboard.c`, `config.h` — small, single-purpose files

## Modes

The status bar always shows the current mode.

| Mode     | Entered with | Notes                                  |
|----------|--------------|----------------------------------------|
| NORMAL   | `Esc`        | default; motions and editing commands  |
| INSERT   | `i a I A o O`| type to insert text, `Esc` to leave    |
| VISUAL   | `v`          | move to extend the selection; `x`/`d` deletes it, `y` yanks it, `Esc` cancels |
| `:`      | `:`          | command line (see below)               |

## Commands

Type `:` then one of:

| Command | Action                          |
|---------|---------------------------------|
| `:w`    | save                            |
| `:q`    | quit (refuses when modified)    |
| `:q!`   | quit without saving             |
| `:wq` / `:x` | save and quit             |

`Esc` cancels a command line.

## Keybindings

| Mode     | Key            | Action                          |
|----------|----------------|---------------------------------|
| any      | `Ctrl-S`       | save                            |
| any      | `Ctrl-Q`       | quit (double-press when dirty)  |
| any      | `Ctrl-F`       | fuzzy finder                    |
| any      | `Ctrl-T`       | toggle terminal pane            |
| any      | `Ctrl-W`       | switch pane focus               |
| normal/visual | `<Space>` + key | leader prefix (see below)   |
| normal   | `h j k l`      | move cursor / arrows            |
| normal   | `w b e`        | word motions                    |
| normal   | `0 $ ^`        | line start / end / first word   |
| normal   | `g` / `G`      | top / bottom of file            |
| normal   | `i a I A o O`  | enter insert mode               |
| normal   | `v`            | enter visual mode               |
| normal   | `x` / `X`      | delete char / backspace         |
| normal   | `dd` / `yy`    | delete line / yank line         |
| normal   | `p` / `P`      | paste after / before            |
| normal   | `u` / `Ctrl-R` | undo / redo                     |
| normal   | `/` `n` `N`    | search, next, prev              |
| normal   | `:`            | command line                    |
| normal   | `Ctrl-D/U`     | half page down/up               |
| normal   | `PgUp`/`PgDn`  | page up/down                    |

In the terminal pane, all non-global keys are forwarded to the shell.

### Leader key

In normal/visual mode, `Space` acts as a leader prefix — press it, then one
of the keys below. The bindings live in `leader_keys[]` in `config.h`, and
the leader key itself is `LEADER_KEY` (change it to any key, e.g. `\`).

| Leader chord  | Action                          |
|---------------|---------------------------------|
| `<Space>f`    | fuzzy finder                    |
| `<Space>t`    | toggle terminal pane            |
| `<Space>w`    | switch pane focus               |
| `<Space>s`    | save                            |
| `<Space>q`    | quit                            |
| `<Space>Y`    | copy yank buffer / selection to the system clipboard |
| `<Space>P`    | paste the system clipboard at the cursor |

A leader followed by an unbound key just cancels the leader; `Esc` also
cancels it.

### Counts & operators

Motions and editing commands take a leading count, like vim: `10e`, `18w`,
`3j`, `5x`, `3p`, `2u`, `3n`. `5G` jumps to line 5. `d`/`y` are operators:
`d2j` deletes 3 lines, `d3w` deletes 3 words, `dG` deletes to the bottom,
`dd`/`yy` take a line count (`3dd` deletes 3 lines). In visual mode, counts
extend the selection (`2j`, `10l`). `Esc` cancels a pending count or operator.

## Explicitly out of scope (for now)

- LSP / DAP
- A runtime plugin system or embedded scripting language
- Undo tree (undo is a permanent, linear base feature — see `patches/README.md`)

These aren't rejected forever — just not part of v0. Anything here can come
back as an optional patch later.

## Design notes

| Component      | Approach                                            |
|-----------------|------------------------------------------------------|
| Buffer          | Gap buffer                                           |
| Rendering       | Raw terminal mode, direct ANSI escapes                |
| Embedded shell  | `forkpty()` + minimal VT100/ANSI subset (à la `st`)   |
| Fuzzy matching  | Subsequence match + fzf-style scoring                 |
| Config          | `config.h`, recompiled — no runtime config language   |
| Syntax (patch)  | tree-sitter via `dlopen`; colors from `queries/<lang>/highlights.scm` |
| Extensibility   | Source patches (`.diff`), applied and recompiled      |

## Prior art / inspiration

- **kilo** — proof a real terminal editor fits in ~1000 lines
- **vis** — suckless-adjacent editor; useful reference for file layout,
  even though it uses Lua where this project uses none
- **st** — reference for the embedded terminal's VT/ANSI parsing
- **suckless.org philosophy** — small core, config.h, patch distribution

## Build

```sh
make
./<editor-name> file.c
```

No build system beyond a single Makefile. No package manager, no vendored
dependencies beyond libc (the optional tree-sitter patch vendors
`tree_sitter/api.h` for ABI stability but loads the runtime with `dlopen`).

## Patches

Features live in `patches/<name>.diff`, one feature per file, applied to the
pristine base (`HEAD`) with `git apply`. The base is never committed with
patches applied — the suckless model.

The patches are an explicit, ordered chain, each documented as depending on
the one before it:

    HEAD (pristine base)
      └─ 01-buffers.diff         multi-buffer + quit-all + buffer fuzzy
           └─ 02-multicursor.diff     multiple cursors
                └─ 03-treesitter.diff     tree-sitter syntax highlighting

Two rules keep the set robust: **undo is linear and frozen in the base** (no
patch ever redefines it), and **each feature owns its state in its own
file** (`MCState` in `mc.c`, `HLState` in `highlight.c`) — patches add a
struct pointer to the Editor, never fields.

    ./patches/apply.sh && make   # apply all + rebuild (idempotent)
    ./patches/remove.sh <name>   # un-apply one patch
    ./patches/mkpatch.sh <name>  # capture current edits as a new patch

Full workflow and conventions: see `patches/README.md`.

## License

MIT
