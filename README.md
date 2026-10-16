<div align="center">

# kitten

**Readable source-tree snapshots for the terminal**

![Version](https://img.shields.io/badge/version-0.1.0-d9a86c?style=flat-square)
![C99](https://img.shields.io/badge/C-C99-555555?style=flat-square&logo=c&logoColor=white)
![POSIX.1-2008](https://img.shields.io/badge/API-POSIX.1--2008-6f665c?style=flat-square)
[![License](https://img.shields.io/badge/license-BSD--2--Clause-d9a86c?style=flat-square)](LICENSE)

[Quick start](#quick-start) · [Usage](#usage) · [Installation](#installation)

<br>

<img src="assets/assets-preview.jpg"
     alt="kitten showing a source tree, inline file contents, and summary counts"
     width="640">

</div>

`kitten` prints a directory tree with file sizes and text previews beneath each
file. Use it to inspect a small project or capture source files for review.
Limit the depth, exclude generated files, and choose how much content to show.

## Quick start

Build with a C99 compiler and `make` on a system with POSIX.1-2008 APIs.
There are no third-party runtime dependencies.

```sh
make
./kitten --no-content src
```

Run `./kitten` to preview the current directory, or `./kitten --help` for help.

## Usage

```text
kitten [OPTION]... [PATH]...
```

| Command | Result |
|:--|:--|
| `./kitten README.md src` | Preview several paths in one run. |
| `./kitten -L 2 -m 64K path/to/project` | Descend at most two levels and preview files up to 64 KiB. |
| `./kitten --exclude=.git --exclude='*.o' --summary .` | Exclude matching entries and show totals. |
| `./kitten --dirs-only -L 3 .` | Show directories up to three levels deep. |
| `./kitten -U --no-content large-directory` | Traverse in filesystem order without collecting names for sorting. |
| `./kitten --language=ru -H src` | Use Russian labels and human-readable sizes. |

Options may appear before or after paths. Use `--` before a path starting with
`-`, for example `./kitten -- -notes.txt`.

<details>
<summary>All options</summary>

| Option | Effect |
|:--|:--|
| `-m BYTES`, `--max-bytes=BYTES` | Preview files up to this size; default: `256K`. |
| `-L DEPTH`, `--depth=DEPTH` | Limit traversal depth; `0` shows only the root. |
| `--exclude=PATTERN` | Skip matching names or paths; repeat to add patterns. |
| `--no-content` | Show the tree and metadata without file contents. |
| `--content=auto\|never` | Enable or disable previews. |
| `--raw-content` | Print preview text without escaping control characters. |
| `--dirs-only` | Show only real directories. |
| `-H`, `--human-readable` | Format sizes in KiB, MiB, GiB, and TiB. |
| `-U`, `--unsorted` | Traverse in filesystem order. |
| `--ascii`, `--unicode` | Select tree characters; UTF-8 locales default to Unicode. |
| `--summary` | Print entry counts, bytes, skips, and errors. |
| `--language=auto\|en\|ru` | Select the message language; default: `auto`. |
| `--version` | Print version and license information. |
| `-h`, `--help` | Print help. |

Size suffixes `K`, `M`, and `G` use powers of 1024 and are case-insensitive.
Options with values also accept a separate argument, such as `--depth 2`.

</details>

## Preview behavior

- Regular text files up to 256 KiB are previewed by default. Larger files are
  skipped; change the limit with `-m`.
- Files containing a NUL byte are marked as binary. Empty files have their own
  label, and special files are never opened for previews.
- Symbolic links are displayed with their targets and are not traversed.
- Names and preview text escape terminal control characters by default.
  `--raw-content` disables escaping for file contents only.

Entries are sorted according to the current locale. Use `-U` for large
directories to avoid keeping their names in memory for sorting.

Exclusions use shell-style patterns, so quote wildcards to pass them to
`kitten`. They apply to directory entries, including hidden files, but not to
paths explicitly supplied as roots. With one directory operand, paths are
relative to that directory. `.gitignore` is not read automatically.

The exit status is `0` on success and `1` for argument, filesystem, or output
errors. Skipping a binary file or a file over the preview limit is not an error.

## Installation

Install for your user:

```sh
make install PREFIX="$HOME/.local"
```

The executable goes in `$HOME/.local/bin`; add that directory to `PATH` if
needed. With no `PREFIX`, `make install` uses `/usr/local/bin` and requires
write access there.

Use the same prefix to uninstall:

```sh
make uninstall PREFIX="$HOME/.local"
```

For packaging, set `DESTDIR` to a staging directory:

```sh
make PREFIX=/usr DESTDIR="$pkgdir" install
```

Both `PREFIX` and `DESTDIR` support paths with spaces.

## Development

[main.c](src/main.c) handles arguments and locale selection,
[walk.c](src/walk.c) handles traversal and previews, and
[output.c](src/output.c) handles formatting and escaping. Shared types and
interfaces are in [kitten.h](src/kitten.h).

The Makefile respects `CC`, `CPPFLAGS`, `CFLAGS`, `LDFLAGS`, and `LDLIBS`.
It compiles as C99 with `-Wall -Wextra -Wpedantic` by default:

```sh
make CC=clang CFLAGS='-O2 -g'
```

Use `make clean` to remove the compiled executable before rebuilding with
different compiler settings.

## License

`kitten` is distributed under the [BSD 2-Clause License](LICENSE).
