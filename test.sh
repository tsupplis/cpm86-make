#!/bin/sh
# Regression tests for CP/M-86 ts (timestamp) and make tools.
# Exercised through emu2.

cd "$(dirname "$0")" || exit 1

LC_ALL=C
export LC_ALL

TIMEOUT=10
fail=0

pass() { printf 'PASS  %-11s %s\n' "$1" "$2"; }

blame() {
	printf 'FAIL  %-11s %s\n' "$1" "$2"
	printf '        argv:     %s\n'     "$3"
	printf '        expected: %s\n'     "$4"
	printf '        got:\n'
	printf '%s\n' "$5" | sed 's/^/        | /'
	fail=1
}

run() {
	binary=$1; shift
	timeout $TIMEOUT emu2 $binary "$@" >out.tmp 2>err.tmp </dev/null
	rc=$?
	out=$(tr -d '\r' <out.tmp)
	err=$(tr -d '\r' <err.tmp)
	diag=$(cat err.tmp out.tmp | tr -d '\r')
}

cleanup() {
	rm -f test_ts.dir test_ts.pag make.sta $$$.SUB
	rm -f out.tmp err.tmp test_*.mk test_*.txt test_*.inc
	rm -f test_main.c test_util.c test_a.c test_b.c test_comm.h
	rm -f test_main.o test_util.o test_app.cmd test_prog.cmd
}

trap cleanup EXIT INT TERM

# Ensure binaries exist
for b in ts.cmd make.cmd; do
	if [ ! -f "$b" ]; then
		echo "ERR: $b missing, please build it first" >&2
		exit 1
	fi
done

cleanup

# ==================================================================== #
echo "== ts.cmd: basic operations =="
# ==================================================================== #

# 1. Query unrecorded file returns 0
run ts.cmd -t test_ts test_main.c
if [ "$out" = "0" ]; then
	pass "ts.cmd" "query unrecorded file returns 0"
else
	blame "ts.cmd" "query unrecorded file returns 0" "-t test_ts test_main.c" "0" "$out"
fi

# 2. Set simulated timestamp
run ts.cmd -t test_ts -s test_main.c 1000
run ts.cmd -t test_ts test_main.c
if [ "$out" = "1000" ]; then
	pass "ts.cmd" "set and query simulated timestamp"
else
	blame "ts.cmd" "set and query simulated timestamp" "-t test_ts -s test_main.c 1000" "1000" "$out"
fi

# 3. Multiple entries and wildcard query
run ts.cmd -t test_ts -s test_util.c 2000
run ts.cmd -t test_ts 'test_*.c'
if printf '%s\n' "$out" | grep -qiF "test_main.c: 1000" && printf '%s\n' "$out" | grep -qiF "test_util.c: 2000"; then
	pass "ts.cmd" "wildcard query matches multiple keys"
else
	blame "ts.cmd" "wildcard query matches multiple keys" "-t test_ts 'test_*.c'" "test_main.c: 1000 and test_util.c: 2000" "$out"
fi

# 4. Wildcard deletion
run ts.cmd -t test_ts -d test_m*.c
run ts.cmd -t test_ts test_main.c
if [ "$out" = "0" ]; then
	pass "ts.cmd" "wildcard delete removes matched key"
else
	blame "ts.cmd" "wildcard delete removes matched key" "-t test_ts -d test_m*.c" "0" "$out"
fi

# 5. Single key deletion
run ts.cmd -t test_ts -d test_util.c
run ts.cmd -t test_ts test_util.c
if [ "$out" = "0" ]; then
	pass "ts.cmd" "exact delete removes single key"
else
	blame "ts.cmd" "exact delete removes single key" "-t test_ts -d test_util.c" "0" "$out"
fi

# 5b. List all records (-l)
run ts.cmd -t test_ts -s test_list1.c 500
run ts.cmd -t test_ts -s test_list2.c 600
run ts.cmd -t test_ts -l
if printf '%s\n' "$out" | grep -qiF "test_list1.c: 500" && printf '%s\n' "$out" | grep -qiF "test_list2.c: 600"; then
	pass "ts.cmd" "-l lists all records"
else
	blame "ts.cmd" "-l lists all records" "-t test_ts -l" "test_list1.c: 500 and test_list2.c: 600" "$out"
fi


# ==================================================================== #
echo "== make.cmd: dependency DAG & -n dry run =="
# ==================================================================== #

# Create test sources
echo "int main(){return 0;}" > test_main.c
echo "int util(){return 0;}" > test_util.c

# Create simple makefile
cat << 'EOF' > test_simple.mk
test_app.cmd: test_main.o test_util.o
	link -o test_app.cmd test_main.o test_util.o

test_main.o: test_main.c
	cc -o test_main.o test_main.c

test_util.o: test_util.c
	cc -o test_util.o test_util.c
EOF

# 6. Dry run on unbuilt project (all commands planned in leaf-first order)
run make.cmd -f test_simple.mk -t test_ts -n test_app.cmd
expected_cmds="cc -o test_main.o test_main.c
cc -o test_util.o test_util.c
link -o test_app.cmd test_main.o test_util.o"

if [ "$out" = "$expected_cmds" ]; then
	pass "make.cmd" "-n dry run plans actions leaf-first"
else
	blame "make.cmd" "-n dry run plans actions leaf-first" "-f test_simple.mk -n test_app.cmd" "$expected_cmds" "$out"
fi

# 7. Up-to-date detection
# Stamp all targets and create files on disk
touch test_main.o test_util.o test_app.cmd
run ts.cmd -t test_ts -s test_main.c 1000
run ts.cmd -t test_ts -s test_util.c 1000
run ts.cmd -t test_ts -s test_main.o 2000
run ts.cmd -t test_ts -s test_util.o 2000
run ts.cmd -t test_ts -s test_app.cmd 3000

run make.cmd -f test_simple.mk -t test_ts -n test_app.cmd
if printf '%s\n' "$out" | grep -qiF "up to date"; then
	pass "make.cmd" "recognizes up to date target"
else
	blame "make.cmd" "recognizes up to date target" "-f test_simple.mk -t test_ts -n test_app.cmd" "INF: Target 'test_app.cmd' is up to date." "$out"
fi

# 8. Rebuild triggered when single source is updated
run ts.cmd -t test_ts -s test_main.c 4000
run make.cmd -f test_simple.mk -t test_ts -n test_app.cmd
expected_rebuild="cc -o test_main.o test_main.c
link -o test_app.cmd test_main.o test_util.o"

if [ "$out" = "$expected_rebuild" ]; then
	pass "make.cmd" "partial rebuild triggered when single source is newer"
else
	blame "make.cmd" "partial rebuild triggered when single source is newer" "-f test_simple.mk -t test_ts -n test_app.cmd" "$expected_rebuild" "$out"
fi


# 8b. Unconditional rebuild (-B)
run make.cmd -f test_simple.mk -t test_ts -n -B test_app.cmd
if [ "$out" = "$expected_cmds" ]; then
	pass "make.cmd" "-B forces unconditional rebuild"
else
	blame "make.cmd" "-B forces unconditional rebuild" "-f test_simple.mk -t test_ts -n -B test_app.cmd" "$expected_cmds" "$out"
fi

# ==================================================================== #
echo "== make.cmd: defines and automatic variables =="
# ==================================================================== #

cat << 'EOF' > test_vars.mk
PIP=a:pip

all: bundle.out

bundle.out: test_p1.out test_p2.out
	$(PIP) $@=test_p1.out,test_p2.out

test_p1.out: test_p1.txt
	$(PIP) $@=$<

test_p2.out: test_p2.txt \
	test_p2.inc
	$(PIP) $@=$*
EOF

echo "p1" > test_p1.txt
echo "p2" > test_p2.txt
echo "inc" > test_p2.inc

run make.cmd -f test_vars.mk -t test_ts -n
expected_vars="a:pip test_p1.out=test_p1.txt
a:pip test_p2.out=test_p2.txt test_p2.inc
a:pip bundle.out=test_p1.out,test_p2.out"

if [ "$out" = "$expected_vars" ]; then
	pass "make.cmd" "expands variables, automatic macros and line continuations"
else
	blame "make.cmd" "expands variables, automatic macros and line continuations" "-f test_vars.mk -n" "$expected_vars" "$out"
fi

rm -f test_p1.txt test_p2.txt test_p2.inc

# ==================================================================== #
echo "== make.cmd: diamond dependencies =="
# ==================================================================== #

echo "int a(){return 0;}" > test_a.c
echo "int b(){return 0;}" > test_b.c
echo "#define COMMON 1"   > test_comm.h

cat << 'EOF' > test_diamond.mk
all: test_prog.cmd

test_prog.cmd: test_a.o test_b.o
	link test_prog.cmd test_a.o test_b.o

test_a.o: test_a.c test_comm.h
	cc test_a.c

test_b.o: test_b.c test_comm.h
	cc test_b.c
EOF

run make.cmd -f test_diamond.mk -t test_ts -n
expected_diamond="cc test_a.c
cc test_b.c
link test_prog.cmd test_a.o test_b.o"

if [ "$out" = "$expected_diamond" ]; then
	pass "make.cmd" "diamond DAG evaluates correctly"
else
	blame "make.cmd" "diamond DAG evaluates correctly" "-f test_diamond.mk -n" "$expected_diamond" "$out"
fi


# ==================================================================== #
echo "== make.cmd: error cases =="
# ==================================================================== #

# 9. Missing prerequisite with no rule
cat << 'EOF' > test_missing.mk
app: missing.c
	cc missing.c
EOF

run make.cmd -f test_missing.mk -n app
if printf '%s\n' "$diag" | grep -qiF "No rule to make target"; then
	pass "make.cmd" "reports error on missing source file"
else
	blame "make.cmd" "reports error on missing source file" "-f test_missing.mk -n app" "ERR: No rule to make target 'missing.c'" "$diag"
fi

# 10. Circular dependency detection
cat << 'EOF' > test_cycle.mk
a: b
	cc a
b: a
	cc b
EOF

run make.cmd -f test_cycle.mk -n a
if printf '%s\n' "$diag" | grep -qiF "Circular dependency"; then
	pass "make.cmd" "detects circular dependency cycle"
else
	blame "make.cmd" "detects circular dependency cycle" "-f test_cycle.mk -n a" "ERR: Circular dependency detected for 'a'" "$diag"
fi

# ==================================================================== #
echo
if [ $fail -eq 0 ]; then
	echo "All regression tests passed successfully!"
else
	echo "SOME TESTS FAILED!"
fi
exit $fail
