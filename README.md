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
commands, search, fuzzy finder, and an embedded terminal pane. Still rough
around the edges — no syntax highlighting, no undo tree.

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
- **Patch-friendly layout**: `buffer.c`, `terminal.c`, `input.c`, `pty.c`,
  `fuzzy.c`, `config.h` — small, single-purpose files

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

## Explicitly out of scope (for now)

- LSP / DAP
- tree-sitter / real syntax highlighting (maybe simple regex highlighting later)
- A runtime plugin system or embedded scripting language
- Undo tree (start with plain linear undo)
- Multiple cursors

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
dependencies beyond libc.

## Patches

Once patches exist, they'll live under `patches/` as plain `.diff` files,
one feature per patch, applied with `patch -p1 < patches/foo.diff`.

## License

MIT
