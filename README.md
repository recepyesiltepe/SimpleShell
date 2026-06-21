# SimpleShell

SimpleShell is a small Unix-like shell written in C. It includes an interactive terminal mode, script execution, a GTK4 graphical wrapper, and a focused set of shell features suitable for a course project or demo shell.

## Requirements

- `gcc`
- `make`
- Python 3
- GTK4 Python bindings for the graphical UI:
  - Arch Linux package names are typically `gtk4` and `python-gobject`.

## Build

```sh
make
```

The binary is written to:

```sh
bin/SimpleShell
```

## Run

Interactive shell:

```sh
./bin/SimpleShell
```

Run a script file:

```sh
./bin/SimpleShell script.sh
```

Run the GTK4 UI:

```sh
make run-ui
```

or:

```sh
./launch_simpleshell_ui.sh
```

## Tests

Run the regression suite:

```sh
make test
```

The tests run the compiled shell as a black box and cover command execution, conditionals, redirection, aliases, environment assignments, expansion, comments, scripts, `source`, `which`, and shell-like command failure statuses.

## AppImage

GitHub Actions builds a GTK4 AppImage on pushes to `main`, pull requests, and manual workflow runs. Download the `SimpleShell-x86_64.AppImage` artifact from the `Build AppImage` workflow run.

The AppImage launches the GTK4 UI and bundles the compiled `SimpleShell` binary, Python launcher, PyGObject, GTK typelibs, and required shared-library dependencies staged from the Ubuntu build runner.

## Supported Shell Features

SimpleShell supports:

- External command execution with `fork` and `execvp`.
- Pipelines with `|`.
- Redirection with `<`, `>`, and `>>`.
- Command sequencing with `;`.
- Conditional execution with `&&` and `||`.
- Background jobs with `&`.
- Builtins:
  - `cd`
  - `pwd`
  - `exit`
  - `export`
  - `unset`
  - `history`
  - `jobs`
  - `fg`
  - `bg`
  - `type`
  - `which`
  - `alias`
  - `unalias`
  - `source`
  - `.`
  - `help`
- History expansion:
  - `!!`
  - `!N`
- Environment assignment prefixes:
  - `FOO=bar command`
  - `FOO=bar` to persist in the current shell.
- Single and double quotes.
- Backslash escaping.
- `#` comments outside quotes.
- Tilde expansion for `~`.
- Basic glob expansion for unquoted arguments.
- Script files with `./bin/SimpleShell script.sh`.
- Sourcing files in the current shell with `source file` or `. file`.
- Custom prompts with `SIMPLESHELL_PROMPT`.
  - `\w` expands to the current working directory.
  - `\?` expands to the last exit status.
  - `\\` emits a literal backslash.
- Shell-like command failure statuses:
  - `127` for command not found.
  - `126` for found but not executable or other exec failure.

## GTK4 UI

The GTK4 UI starts `bin/SimpleShell` inside a pseudo-terminal. This lets terminal-aware commands, including `sudo`, prompt correctly.

The UI includes:

- Scrollable console output.
- Keyboard input forwarded to the pseudo-terminal.
- ANSI color handling for the shell prompt.
- Filtering for common terminal control sequences.
- Clear button.
- Light/dark theme toggle.
- Font size controls.
- Saved UI preferences in `~/.simpleshell_ui.json`.

## Known Limitations

SimpleShell is intentionally smaller than a POSIX shell. It does not currently implement:

- Command substitution such as `$(...)`.
- Shell functions.
- Subshell grouping with `( ... )`.
- Advanced variable expansion.
- Full POSIX quoting and word-splitting semantics.
- Complete terminal job control with process groups for every foreground pipeline.

These are reasonable future extensions, but they are outside the scope of a simple shell.

## Clean

```sh
make clean
```
