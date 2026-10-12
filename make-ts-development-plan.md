# CP/M-86 Make & Timestamp (ts) Development Plan

## Top-Level Overview

This project implements a streamlined `make` build tool, a disk-backed build state manager (`state` API), and a companion `ts` (timestamp management) utility specifically engineered for CP/M-86 1.1+. 

Because CP/M-86 lacks native file timestamps, process execution primitives (`fork`/`exec`), and process exit return codes, this system bridges those limitations through:
1. **Timestamp Database (`ts`)**: Persistent file modification timestamps backed by `ndbm.lib` and `epoch.lib` (`epoch_now()`), with wildcard query/deletion support via `dirent` / FCB matching. Configurable database via `-t <db>` (default: `ts`). Supports explicit timestamp values (`ts -s <file> [val]`) for environments lacking BDOS clock support.
2. **Disk-Backed State Management (`state.h` / `state.c`)**: A clean, modular disk state API (persisted to `make.sta`) to maintain target build queues, leaf target stack position, and target creation status across CCP batch re-invocations.
3. **Batch Execution via `$$$.SUB` (`sub.lib`)**: Make constructs a dependency DAG and emits build actions and state transitions into the CCP submit file stack (`$$$.SUB`).
4. **Make Options**: `make [-f makefile] [-t db] [-n] [target]` supporting alternative makefiles (default: `makefile`), alternative timestamp databases (`-t`), and dry-run test mode (`-n` which bypasses epoch retrieval and suppresses `$$$.SUB`).
5. **Target Verification**: Target existence and timestamp progression replace missing process return codes.
6. **Standard Diagnostics**: `INF:`, `WRN:`, and `ERR:` logging and usage patterns following the `cmdinfo.c` reference.
7. **BDOS Guard**: Enforced when retrieving system clock (`epoch_now()`), ensuring BDOS version <= 2.2.

---

## Sub-Tasks

### Sub-Task 1: Timestamp Utility (`ts`) Implementation and Unit Verification
- **Intent**: Transform `ts.c` into a robust timestamp utility (`ts.cmd`) that queries, sets, and deletes timestamps in an `ndbm` database with wildcard support, simulated timestamp injection, and `cmdinfo.c`-style CLI formatting.
- **Expected Outcomes**:
  - `ts` tool supports:
    - `ts [-t db] <filepattern...>`: query timestamp for files (prints `INF: <file> <epoch>` or `0` if not recorded).
    - `ts [-t db] -s <file> [timestamp]`: set file timestamp to current Unix epoch using `epoch_now()` (requires BDOS <= 2.2), or to explicit integer value if provided without requiring clock access.
    - `ts [-t db] -d <filepattern...>`: delete matching timestamp entries from the database.
    - Default database is `ts` (`ts.dir` / `ts.pag`).
  - BDOS <= 2.2 guard checked when calling `epoch_now()`.
  - Logging adhering to `INF:`, `WRN:`, `ERR:` prefix standards.
  - Regression tests in `test.sh` covering `ts` queries, sets, deletes, simulated values, wildcards, and edge cases.
- **Todo List**:
  - [ ] Implement conditional BDOS version checking guard in `ts.c` only when `epoch_now()` is invoked without explicit timestamp.
  - [ ] Refactor argument parsing in `ts.c` to support `-t <db>`, `-s [val]`, `-d`, and query patterns.
  - [ ] Integrate `epoch_now()` from `epoch.lib` when no explicit timestamp is supplied.
  - [ ] Implement wildcard matching across DB keys / disk files using `dirent` and string helpers.
  - [ ] Format diagnostic and standard outputs to match `cmdinfo.c` conventions.
  - [ ] Update `Makefile` build rules for `ts.cmd` linking `ts.o`, `ndbm.lib`, `epoch.lib`, `util.lib`, and `sub.lib`.
- **Relevant Context**:
  - [ts.c](ts.c:1)
  - [../cpm86-cmdtools/cmdinfo.c](../cpm86-cmdtools/cmdinfo.c:1)
  - [epoch.h](epoch.h:1)
  - [ndbm.h](ndbm.h:1)
  - [dirent.h](dirent.h:1)
- **Status**: [x] done

---

### Sub-Task 2: State Management Module (`state.h` / `state.c`)
- **Intent**: Implement a dedicated, clean API for managing Make build state and target stack persistence on disk across CCP batch steps.
- **Expected Outcomes**:
  - Standardized C API (`state_open`, `state_push_target`, `state_pop_target`, `state_peek_target`, `state_save`, `state_load`, `state_delete`, `state_has_pending`).
  - State file `make.sta` serializes active target, remaining stack of targets to build, makefile path, and timestamp database name.
  - Enables Make to safely verify target creation on disk after CCP batch execution and progress through multi-stage dependency trees.
- **Todo List**:
  - [ ] Design `state.h` interface with types `make_state_t` and `state_target_t`.
  - [ ] Implement state file serialization and deserialization in `state.c`.
  - [ ] Implement target stack manipulation (push, pop, peek, clear).
  - [ ] Add state file cleanup on successful build or fatal abort.
- **Relevant Context**:
  - [sub.h](sub.h:1)
  - [sub.c](sub.c:1)
  - [util.h](util.h:1)
- **Status**: [x] done

---

### Sub-Task 3: Makefile Parser and Dependency Graph Engine
- **Intent**: Build a lightweight, robust parser for Makefiles in CP/M-86 and construct an in-memory dependency Directed Acyclic Graph (DAG) for target evaluation.
- **Expected Outcomes**:
  - Parser reads `makefile` (or custom `-f <file>`) line-by-line using `sub_readln` or `fgets`.
  - Handles comments (`#`), whitespace trimming, target dependency lines (`target: dep1 dep2 ...`), and tab/space indented recipe lines.
  - Evaluates single target requested on command line (or defaults to the first target defined).
  - Traverses dependencies recursively, detecting circular dependency errors.
  - Compares timestamps via `ndbm` database (`ts` or custom `-t db`) and file existence on disk to determine out-of-date status.
- **Todo List**:
  - [ ] Define data structures for targets, dependencies, and command lists (`make_target_t`, `make_dep_t`, `make_cmd_t`).
  - [ ] Implement Makefile reader supporting lines up to 128 characters, comments, and indented recipe commands.
  - [ ] Implement DAG builder and topological sort/stack ordering (leaf dependencies first).
  - [ ] Integrate timestamp lookup from `ndbm` database (`ts` or custom `-t db`) to evaluate freshness against prerequisites and disk presence.
  - [ ] Add circular dependency cycle detection and error reporting.
- **Relevant Context**:
  - [make.c](make.c:1)
  - [ndbm.h](ndbm.h:1)
  - [sub.h](sub.h:1)
  - [util.h](util.h:1)
- **Status**: [x] done

---

### Sub-Task 4: Make Execution Engine and `$$$.SUB` State Machine
- **Intent**: Implement the batch execution generator and state machine using `sub.lib` and `state.lib` to sequence out-of-date target commands and progress tracking under CP/M-86 CCP.
- **Expected Outcomes**:
  - Supports CLI flags: `make [-f makefile] [-t db] [-n] [target]`.
  - When `-n` (dry run / test mode) is passed:
    - Bypasses epoch retrieval (`epoch_now()` is not called).
    - Prints the exact sequence of commands without executing or modifying `make.sta` or `$$$.SUB`.
  - In normal build mode:
    - If resuming from `make.sta`: verifies previous target was created on disk, updates its timestamp via `ts -t <db> -s <target>`, pops it, and proceeds to next pending target.
    - Emits tool commands for next leaf target into `$$$.SUB`.
    - Appends `make [-f makefile] [-t db]` to `$$$.SUB` to resume the build loop.
    - Hands control over to CCP via `sub_exit()`.
  - When targets have no commands or all dependencies are up to date, reports `INF: Target '<target>' is up to date.`
  - Validates missing prerequisites on disk (if no rule to build them) with appropriate `ERR:` diagnostics.
- **Todo List**:
  - [ ] Implement CLI parser for `make` supporting `-f <file>`, `-t <db>`, `-n`, and optional `[target]`.
  - [ ] Implement `-n` dry run command tracing and plan display with epoch call bypass.
  - [ ] Integrate `state.c` loading, verification, and stack progression.
  - [ ] Implement submit batch builder using `sub.lib` (`sub_open(SUB_CREATE)`, writing commands in reverse execution order, `sub_close()`).
  - [ ] Implement target existence validation logic (fail if target not created).
  - [ ] Handle clean exit / warm boot handover to CCP when batch is submitted.
- **Relevant Context**:
  - [sub.h](sub.h:1)
  - [sub.c](sub.c:1)
  - [subfile.c](subfile.c:1)
  - [make.c](make.c:1)
- **Status**: [x] done

---

### Sub-Task 5: Build System Integration and Toolchain Validation
- **Intent**: Integrate `make.cmd` into `Makefile`, configure compilation and stripping for Aztec C86, and verify executable production.
- **Expected Outcomes**:
  - `Makefile` compiles `ts.cmd` and `make.cmd` linking `state.o`, `sub.lib`, `ndbm.lib`, `epoch.lib`, and `util.lib`.
  - Executables are properly stripped with `aztec42_sqz` for minimal binary footprint.
  - Clean build without warnings under Aztec C86.
- **Todo List**:
  - [ ] Add `state.o`, `make.o`, `make.cmd` build and link targets to `Makefile`.
  - [ ] Verify clean build with `make clean && make`.
  - [ ] Verify image generation rules for `cpmtest.img`.
- **Relevant Context**:
  - [Makefile](Makefile:1)
  - [cpm86.cfg](cpm86.cfg:1)
- **Status**: [x] done

---

### Sub-Task 6: Emulator (emu2) Test Suite with Timestamp Simulation & Dry-Run Validation
- **Intent**: Build a comprehensive automated test suite executable under `emu2` without requiring a real CP/M-86 hardware clock or BDOS 49/sysdata tod support.
- **Expected Outcomes**:
  - Test suite in `test.sh` runs completely in `emu2` environment.
  - Exercises `ts` with simulated explicit timestamps (`ts -s <file> <val>`), queries, deletes, and wildcard patterns across custom database names (`-t`).
  - Exercises `make -n` dry run across diverse test Makefiles (single target, multi-stage DAGs, diamond dependencies, up-to-date targets, missing dependencies, circular dependencies).
  - Tests verify exact command execution plans and error diagnostic outputs.
- **Todo List**:
  - [ ] Create synthetic test Makefiles representing real-world build DAG scenarios.
  - [ ] Add `ts` regression test cases in `test.sh` for simulated timestamps and queries.
  - [ ] Add `make -n` regression test cases in `test.sh` validating planned execution orders.
  - [ ] Add failure test cases for missing targets, syntax errors, and dependency loops.
  - [ ] Run full test suite and confirm 100% pass rate under `emu2`.
- **Relevant Context**:
  - [test.sh](test.sh:1)
- **Status**: [x] done
