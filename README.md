# CP/M-86 Make Utility

Simple build automation, state management, and timestamp tracking for CP/M-86 1.1+ and above.

---

## Overview

CP/M-86 1.1 (BDOS 2.2) lacks file timestamps, sub-process execution (`fork`/`exec`), and process exit codes. 
`make` and `ts` resolve this through persistent indexed timestamps (`ndbm`), disk-backed state management (`make.sta`), and CCP batching (`$$$.SUB`).

BDOS >= 3.0 is now supported (Concurrent CP/M-86, Concurrent DOS ...). For process management, p_cli is now used without a submit loop. so commands are executed iteratively. Right now process exit codes are not yet scrutinized and file timestamp is not assumed. It will come in subsequent releases. Currently Concurrent CP/M-86, Concurrent DOS 4.1.

DOS Plus is not supported as it does not implement MP/M and CCP/M process management. It is really ok as sophisticated DOS Make utilities are widely available from Concurrent DOS >= 4.1.

![make running on CP/M-86](images/make.png)

---

## Command Reference

### `ts` (Timestamp Management Utility)

Manages persistent epoch modification timestamps stored in an `ndbm` database (`<db>.dir` / `<db>.pag`, default `ts`). Supports explicit timestamp values for testing in environments without BDOS clock support.

```
Usage:
  ts [-t db] <filepattern...>         Query timestamp for files (0 if not recorded)
  ts [-t db] -s <file> [timestamp]   Set file timestamp (system epoch or explicit value)
  ts [-t db] -d <filepattern...>       Delete file timestamp records
  ts [-t db] -l                       List all recorded timestamps
```

**Options:**
- `-t <db>`: Alternate timestamp database name (default: `ts`).
- `-s`: Stamp file with current system epoch (calls `epoch.lib` and enforces BDOS <= 2.2 guard if `[timestamp]` is omitted; uses given integer if specified).
- `-d`: Remove matching entries from database (supports wildcards `*`, `?`).
- `-l`: List all recorded timestamps in database.

**Examples:**
```
A>ts *.c                   ; Query timestamps for all C source files
A>ts -s foo.cmd            ; Record current epoch timestamp (requires clock/CP/M-86)
A>ts -s foo.cmd 1712000000 ; Record explicit simulated timestamp (no clock guard needed)
A>ts -t myproj -s main.o   ; Record timestamp in myproj.db
A>ts -d *.o                ; Delete all .o records from ts.db
```

---

### `make` (CP/M-86 Build Utility)

Evaluates prerequisite rules, tracks build state on disk via `state` API, and schedules batches via `$$$.SUB`.

```
Usage:
  make [-f makefile] [-t db] [-a] [-r] [-n] [-v] [target]
```

**Options:**
- `-f <file>`: Alternate makefile (default: `makefile`).
- `-t <db>`: Alternate timestamp database name (default: `ts`).
- `-a`: Unconditional rebuild (forces all targets to be rebuilt).
- `-r`: Reset build state (restores an active output backup when valid state is available, then clears `make.sta`). A dry run never resets state.
- `-n`: Dry run (test mode) - bypasses epoch retrieval, prints action plan without modifying disk or `$$$.SUB`.
- `-v`: Verbose mode - traces dependency evaluation, out-of-date reasons, and batch creation.
- `target`: Target to build (defaults to first target in `makefile`).

**Makefile Syntax & Variables:**

- **Explicit Rules Only**: In keeping with the minimalist CP/M-86 environment, all dependency rules and recipes must be explicit. Pattern rules (e.g. `%.o: %.c`) and suffix rules (e.g. `.c.o:`) are **not** supported.
- **Variable Definitions**: Defined at the top level using `NAME = VALUE` or `NAME=VALUE` (e.g. `PIP=a:pip`, `CC=cc -o $@`). Variable names are matched case-insensitively.
- **Variable Expansion**: Evaluated inside recipe command lines using `$(VAR)`, `${VAR}`, or `$V`.
- **Automatic Variables**:
  - `$@` : Evaluates to the target name.
  - `$<` : Evaluates to the first prerequisite name.
  - `$*` / `$^` : Evaluates to all prerequisites (space-separated).
  - `$$` : Literal `$` character.
- **Line Continuations**: Lines ending with `\` are joined with the subsequent line.

```makefile
# Simple dependency rule with variables and automatic macros
.phony: all clean

PIP=a:pip

all: bundle.out

bundle.out: part1.out \
	part2.out
	$(PIP) $@=part1.out,part2.out

part1.out: part1.txt
	$(PIP) $@=$<

part2.out: part2.txt
	$(PIP) $@=$*

clean:
	era *.out
	ts -d *.out
```

### Build validation and recovery

File recipes must recreate their target. Make preserves the old output as
`make.bak`, restores it if no replacement appears, and timestamps successful
replacements. Partial outputs cannot be distinguished from successful builds
without child exit status.

A bare `make` resumes the saved build; `-r` resets it and restores any recorded
backup. Inspect `make.bak` manually if state is damaged. Dry runs preserve state.

Limits: 32 targets, 16 prerequisites and commands per target, 32 variables,
127-character logical lines, and 125-character expanded commands. Limit and
write errors stop the build; failed submit writes roll back the appended batch.

---

## Diagnostics Standard

Output follows `cmdinfo` format:
- `INF: <message>` : Normal status and information
- `WRN: <message>` : Non-fatal warning
- `ERR: <message>` : Fatal error leading to exit

---

## Build

Requires the `cpm86-crossdev` toolchain (`aztec42_cc`, `aztec42_link`, etc.).

The project also depends on the `util` library from
[cpm86-hacking](https://github.com/tsupplis/cpm86-hacking) and on the `ndbm`
library from [cpm86-ports](https://github.com/tsupplis/cpm86-ports). The
following files are taken from there and kept in this directory; they are not
built here:

| File | Source | Purpose |
|------|--------|---------|
| `util.lib` | cpm86-hacking | Library linked into `ts.cmd` and `make.cmd` |
| `util.h`, `conio.h`, `debug.h`, `dirent.h`, `epoch.h`, `gfx.h`, `sub.h` | cpm86-hacking | Headers used at compile time |
| `ndbm.lib` | cpm86-ports | Indexed timestamp database library linked into `ts.cmd` and `make.cmd` |
| `ndbm.h` | cpm86-ports | Header used at compile time |

```sh
make          # builds ts.cmd and make.cmd
make test-local # runs isolated regression and fault-injection checks
make test       # runs those checks, then launches the CP/M emulator
make clean    # removes all generated files
```

The local tests also require Python 3 and a host C compiler (`cc`, or
`CC_HOST`). They run the original checks, 16-bit memory/limit and state-resume
regressions, and fault-injection harnesses compiled with both the host compiler
and Aztec. All test files are kept in temporary directories.

---

## Architecture

`make.c` parses and plans builds; `state.c` saves progress. `recipe.c` handles
output backup and validation for both `subproc.c` and `execproc.c`.

### Component Interaction

```mermaid
flowchart LR
    MF[makefile] --> MK[make.cmd]
    MK <-->|Progress| STA[make.sta]
    MK <-->|Timestamps| DB[ts.dir / ts.pag]
    MK <-->|Preserve / restore| BAK[make.bak]
    MK -->|CP/M-86: batch| SUB[$$$.SUB]
    SUB -->|Warm boot| CCP[CCP]
    CCP -->|Recipes| TOOLS[Build tools]
    CCP -->|Resume| MK
    MK -->|CCP/M-86: p_cli| TOOLS
    TOOLS --> TGT[Target files]
    TGT -->|Validate output| MK
```

### Execution Lifecycle

```mermaid
flowchart TD
    A[make invocation] --> B{Dry run?}
    B -->|Yes| C[Parse, plan and print commands]
    B -->|No| D[Load saved build or initialize new build]
    D --> E[Parse selected makefile and resume or plan dependencies]
    E --> F{Pending target?}
    F -->|No| END[Delete state and finish]
    F -->|Yes| G{Has recipe?}
    G -->|No| V{File exists or target is phony?}
    G -->|Yes| H[Invalidate timestamp, preserve old output, save state]
    H --> I{Execution mode?}
    I -->|CP/M-86| J[Append recipe and resume command to submit queue]
    J --> K[Warm boot: CCP executes recipe and restarts make]
    K --> L[Reload state and saved makefile]
    L --> V
    I -->|CCP/M-86| M[Launch commands and wait for console attachment]
    M --> V
    J -->|Write failure| ERR[Roll back batch, restore backup and stop]
    M -->|Launch failure| ERR
    V -->|No| ERR
    V -->|Yes| N[Timestamp file, remove backup and save progress]
    N --> F
```

Phony targets skip file validation and timestamps. Make updates the timestamp
database itself; no `ts -s` command is added to the submit queue.

### Dependency Decision Logic

```mermaid
flowchart TD
    A{Forced or phony?} -->|Yes| Build[Build target]
    A -->|No| T{Target file exists?}
    T -->|No| Build
    T -->|Yes| S{Recorded timestamp?}
    S -->|No| Build
    S -->|Yes| P{Any prerequisite needed or newer?}
    P -->|Yes| Build
    P -->|No| OK[Up to date]
```

---

## Companion projects

| Project | Description |
|---------|-------------|
| [cpm86-kernel](https://github.com/tsupplis/cpm86-kernel)     | CP/M-86 1.1 distribution rebuilt from patched and reconstituted sources |
| [ccpm86-y2k](https://github.com/tsupplis/ccpm86-y2k)         | CCP/M-86 3.1 distribution rebuilt from patched and reconstituted sources |
| [cpm86-crossdev](https://github.com/tsupplis/cpm86-crossdev) | Unix CP/M-86 cross development project (compilers, emulation and tools) |
| [cpm86-hacking](https://github.com/tsupplis/cpm86-hacking)   | CP/M-86 miscellaneous tools and PCE emulator helpers |
| [cpm86-cmdtools](https://github.com/tsupplis/cpm86-cmdtools) | CP/M-86 `.cmd` file manipulation tools |
| [cpm86-ports](https://github.com/tsupplis/cpm86-ports)       | CP/M-86 application ports in C and assembler |
| [cpm86-vi](https://github.com/tsupplis/cpm86-vi)             | STevie vi port for CP/M-86 and PC-DOS 1.1 |
| [cpm86-msbasic](https://github.com/tsupplis/cpm86-msbasic)   | A recreaction of msbasic-86 for CP/M-86 and PC-DOS 1.1 from gwbasic sources |
| [cpm86-menu](https://github.com/tsupplis/cpm86-menu)   | A submit replacement and a menu system for submit orchestration for CP/M-86 1.1 |
| [cpm86-make](https://github.com/tsupplis/cpm86-make)   | A simple submit based make tool for CP/M-86 1.1 |
| [pcdos11-hacking](https://github.com/tsupplis/pcdos11-hacking) | PC-DOS 1.1 distribution, tools and notes |

## License

MIT — see [LICENSE](LICENSE.md).
