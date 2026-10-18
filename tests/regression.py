#!/usr/bin/env python3
"""Run all checks in disposable directories; never touch a user's build state."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
checks = 0


def run(args, cwd):
    p = subprocess.run(args, cwd=cwd, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True, timeout=15)
    if p.returncode:
        raise AssertionError(f"{args}: exit {p.returncode}\n{p.stdout}")
    return p.stdout.replace("\r", "")


def check(ok, description):
    global checks
    if not ok:
        raise AssertionError(description)
    checks += 1
    print(f"PASS  safety      {description}", flush=True)


def state_text(makefile="saved.mk", target="done", current="done", backup="", queue=()):
    return (f"version=2\nmakefile={makefile}\ndbname=probe\nmain_target={target}\n"
            f"current_target={current}\nbackup={backup}\ncount={len(queue)}\n"
            + "".join(f"target={t}\n" for t in queue) + "end=make\n")


with tempfile.TemporaryDirectory(prefix="cpm86-make-") as folder:
    work = Path(folder)
    for name in ("make.cmd", "ts.cmd", "test.sh"):
        shutil.copy2(ROOT / name, work / name)
    print(run(["sh", "test.sh"], work), end="")

    def make(*args):
        return run(["emu2", "make.cmd", *args], work)

    def reject(text, description, *args):
        (work / "limits.mk").write_text(text)
        output = make("-n", "-f", "limits.mk", *args)
        check("ERR:" in output and "echo " not in output, description)

    # Exactly 32 targets: all plus 31 recipes, each with one dependency.
    chain = "all: t0\n" + "".join(
        f"t{i}:" + (f" t{i+1}" if i < 30 else "") + f"\n\techo TARGET{i}\n"
        for i in range(31))
    (work / "large.mk").write_text(chain)
    output = make("-n", "-f", "large.mk")
    check(output.strip().splitlines() == [f"echo TARGET{i}" for i in range(30, -1, -1)],
          "all 32 targets fit the 16-bit build and preserve dependency order")
    reject("all:\n" + "".join(f"\techo CMD{i}\n" for i in range(17)), "17th command is a fatal error")
    reject("all: " + " ".join(f"d{i}" for i in range(17)) + "\n", "17th prerequisite is a fatal error")
    reject("".join(f"t{i}:\n" for i in range(33)), "33rd target is a fatal error")
    reject("".join(f".phony: t{i}\n" for i in range(33)), "phony declarations enforce the target limit")
    reject("".join(f"V{i}=value\n" for i in range(33)) + "all:\n", "33rd variable is a fatal error")
    reject("x" * 64 + ":\n", "overlong target names are rejected")
    reject("all: " + "x" * 64 + "\n", "overlong prerequisite names are rejected")
    reject("x" * 32 + "=value\nall:\n", "overlong variable names are rejected")
    reject("all:\n\techo " + "x" * 130 + "\n", "overlong physical lines are rejected")
    reject("all:\n\techo " + "x" * 65 + "\\\n" + "y" * 65 + "\n", "overlong continued lines are rejected")
    reject("X=" + "x" * 90 + "\nall:\n\techo $(X)$(X)\n", "overlong expanded commands are rejected")
    reject("all:\n\techo $(BROKEN\n", "unterminated variable expansions are rejected")
    (work / "exact.mk").write_text("all:\n\t" + "x" * 125 + "\n")
    check(make("-n", "-f", "exact.mk").strip() == "x" * 125, "125-character CCP command remains valid")
    reject("all:\n\t" + "x" * 126 + "\n", "126-character commands are rejected before submit")
    (work / "eof.mk").write_bytes(b"all:\r\n\techo OK\x1agarbage: missing\n")
    check(make("-n", "-f", "eof.mk").strip() == "echo OK", "CP/M EOF terminates an unterminated final line")

    # Maximal recipes must fail cleanly on OOM, rather than wrap a size or hang.
    (work / "memory.mk").write_text("".join(
        f"t{i}:\n" + ("\t" + "x" * 125 + "\n") * 16 for i in range(32)))
    check("Not enough memory" in make("-n", "-f", "memory.mk"), "heap exhaustion fails cleanly")

    state = work / "make.sta"
    (work / "saved.mk").write_text(".phony: done\ndone:\n")
    (work / "other.mk").write_text(".phony: other\nother:\n")
    for args in (("-f", "other.mk"), ("-t", "other"), ("other",)):
        state.write_text(state_text())
        before = state.read_bytes()
        check("Active build differs" in make(*args) and state.read_bytes() == before,
              f"conflicting invocation {args} preserves active state")
    check("Build complete" in make() and not state.exists(), "bare resume parses the saved alternate makefile")
    for damaged in ("makefile=saved.mk\ncount=0\n", state_text().replace("end=make\n", ""),
                    state_text().replace("count=0", "count=1"), state_text() + "target=extra\n"):
        state.write_text(damaged)
        check("Invalid make.sta" in make() and state.read_text() == damaged,
              "legacy or corrupt state is rejected without changing it")
    state.write_text(state_text())
    before = state.read_bytes()
    check("ERR:" not in make("-n", "-r", "-f", "other.mk") and state.read_bytes() == before,
          "dry-run reset never modifies build state")
    check("Cannot open" in make("-f", "missing.mk", "-n") and state.read_bytes() == before,
          "makefile parse errors preserve active state")
    state.unlink()

    # Exercise restoration and completion using the actual Aztec runtime/BDOS shim.
    (work / "saved.mk").write_text("result.o:\n\tcompiler result.o\n")
    (work / "make.bak").write_text("OLD")
    state.write_text(state_text(target="result.o", current="result.o", backup="make.bak"))
    check("not created" in make() and (work / "result.o").read_text() == "OLD"
          and not (work / "make.bak").exists() and not state.exists(),
          "missing replacement restores the original using the Aztec rename API")
    (work / "make.bak").write_text("OLD")
    (work / "result.o").write_text("NEW")
    state.write_text(state_text(target="result.o", current="result.o", backup="make.bak"))
    check("Build complete" in make() and (work / "result.o").read_text() == "NEW"
          and not (work / "make.bak").exists() and not state.exists(),
          "successful resumed replacement removes its backup and state")
    check(run(["emu2", "ts.cmd", "-t", "probe", "result.o"], work).strip().isdigit(),
          "resumed replacement stores a timestamp")

    # Compile the same injection harness with the host compiler and with Aztec.
    harness = work / "faults"
    harness.mkdir()
    for name in ("make.h", "state.h", "state.c", "recipe.c", "subproc.c", "execproc.c", "util.h", "sub.h", "ndbm.h"):
        shutil.copy2(ROOT / name, harness / name)
    shutil.copy2(ROOT / "tests/faults.c", harness / "faults.c")
    cc = os.environ.get("CC_HOST", "cc")
    run([cc, "-std=gnu89", "-I.", "-Dstate_save=real_state_save", "-c", "state.c", "-o", "hstate.o"], harness)
    run([cc, "-std=gnu89", "-I.", "-DMAKE_HOST_TEST", "faults.c", "subproc.c", "execproc.c", "recipe.c", "hstate.o", "-o", "faults"], harness)
    check("PASS: execution" in run(["./faults"], harness), "host fault injection, including buffered state write failure")
    run(["aztec42_cc", "-I.", "+F", "+0", "-Dstate_save=real_state_save", "-o", "fstate.o", "state.c"], harness)
    for name in ("faults", "subproc", "execproc", "recipe"):
        run(["aztec42_cc", "-I.", "+F", "+0", "-o", f"{name}.o", f"{name}.c"], harness)
    run(["aztec42_link", "-o", "faults.cmd", "faults.o", "fstate.o", "subproc.o", "execproc.o", "recipe.o", "-lc86"], harness)
    check("PASS: execution" in run(["emu2", "faults.cmd"], harness), "Aztec fault injection validates 16-bit recovery and submit ordering")

print(f"All {checks} safety checks and the original regression suite passed.")
