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
	rm -f testts.dir testts.pag make.sta '$$$.SUB'
	rm -f out.tmp err.tmp test*.mk test*.txt test*.inc
	rm -f testmain.c testutil.c testa.c testb.c testcomm.h
	rm -f testmain.o testutil.o testapp.cmd testprog.cmd
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
run ts.cmd -t testts testmain.c
if [ "$out" = "0" ]; then
	pass "ts.cmd" "query unrecorded file returns 0"
else
	blame "ts.cmd" "query unrecorded file returns 0" "-t testts testmain.c" "0" "$out"
fi

# 2. Set simulated timestamp
run ts.cmd -t testts -s testmain.c 1000
run ts.cmd -t testts testmain.c
if [ "$out" = "1000" ]; then
	pass "ts.cmd" "set and query simulated timestamp"
else
	blame "ts.cmd" "set and query simulated timestamp" "-t testts -s testmain.c 1000" "1000" "$out"
fi

# 3. Multiple entries and wildcard query
run ts.cmd -t testts -s testutil.c 2000
run ts.cmd -t testts 'test*.c'
if printf '%s\n' "$out" | grep -qiF "testmain.c: 1000" && printf '%s\n' "$out" | grep -qiF "testutil.c: 2000"; then
	pass "ts.cmd" "wildcard query matches multiple keys"
else
	blame "ts.cmd" "wildcard query matches multiple keys" "-t testts 'test*.c'" "testmain.c: 1000 and testutil.c: 2000" "$out"
fi

# 4. Wildcard deletion
run ts.cmd -t testts -d testm*.c
run ts.cmd -t testts testmain.c
if [ "$out" = "0" ]; then
	pass "ts.cmd" "wildcard delete removes matched key"
else
	blame "ts.cmd" "wildcard delete removes matched key" "-t testts -d testm*.c" "0" "$out"
fi

# 5. Single key deletion
run ts.cmd -t testts -d testutil.c
run ts.cmd -t testts testutil.c
if [ "$out" = "0" ]; then
	pass "ts.cmd" "exact delete removes single key"
else
	blame "ts.cmd" "exact delete removes single key" "-t testts -d testutil.c" "0" "$out"
fi

# 5b. List all records (-l)
run ts.cmd -t testts -s testlist1.c 500
run ts.cmd -t testts -s testlist2.c 600
run ts.cmd -t testts -l
if printf '%s\n' "$out" | grep -qiF "testlist1.c: 500" && printf '%s\n' "$out" | grep -qiF "testlist2.c: 600"; then
	pass "ts.cmd" "-l lists all records"
else
	blame "ts.cmd" "-l lists all records" "-t testts -l" "testlist1.c: 500 and testlist2.c: 600" "$out"
fi


# ==================================================================== #
echo "== make.cmd: dependency DAG & -n dry run =="
# ==================================================================== #

# Create test sources
echo "int main(){return 0;}" > testmain.c
echo "int util(){return 0;}" > testutil.c

# Create simple makefile
cat << 'EOF' > testsimpl.mk
testapp.cmd: testmain.o testutil.o
	link -o testapp.cmd testmain.o testutil.o

testmain.o: testmain.c
	cc -o testmain.o testmain.c

testutil.o: testutil.c
	cc -o testutil.o testutil.c
EOF

# 6. Dry run on unbuilt project (all commands planned in leaf-first order)
run make.cmd -f testsimpl.mk -t testts -n testapp.cmd
expected_cmds="cc -o testmain.o testmain.c
cc -o testutil.o testutil.c
link -o testapp.cmd testmain.o testutil.o"

if [ "$out" = "$expected_cmds" ]; then
	pass "make.cmd" "-n dry run plans actions leaf-first"
else
	blame "make.cmd" "-n dry run plans actions leaf-first" "-f testsimpl.mk -n testapp.cmd" "$expected_cmds" "$out"
fi

# 7. Up-to-date detection
# Stamp all targets and create files on disk
touch testmain.o testutil.o testapp.cmd
run ts.cmd -t testts -s testmain.c 1000
run ts.cmd -t testts -s testutil.c 1000
run ts.cmd -t testts -s testmain.o 2000
run ts.cmd -t testts -s testutil.o 2000
run ts.cmd -t testts -s testapp.cmd 3000

run make.cmd -f testsimpl.mk -t testts -n testapp.cmd
if printf '%s\n' "$out" | grep -qiF "up to date"; then
	pass "make.cmd" "recognizes up to date target"
else
	blame "make.cmd" "recognizes up to date target" "-f testsimpl.mk -t testts -n testapp.cmd" "INF: Target 'testapp.cmd' is up to date." "$out"
fi

# 8. Rebuild triggered when single source is updated
run ts.cmd -t testts -s testmain.c 4000
run make.cmd -f testsimpl.mk -t testts -n testapp.cmd
expected_rebuild="cc -o testmain.o testmain.c
link -o testapp.cmd testmain.o testutil.o"

if [ "$out" = "$expected_rebuild" ]; then
	pass "make.cmd" "partial rebuild triggered when single source is newer"
else
	blame "make.cmd" "partial rebuild triggered when single source is newer" "-f testsimpl.mk -t testts -n testapp.cmd" "$expected_rebuild" "$out"
fi


# 8b. Unconditional rebuild (-a)
run make.cmd -f testsimpl.mk -t testts -n -a testapp.cmd
if [ "$out" = "$expected_cmds" ]; then
	pass "make.cmd" "-a forces unconditional rebuild"
else
	blame "make.cmd" "-a forces unconditional rebuild" "-f testsimpl.mk -t testts -n -a testapp.cmd" "$expected_cmds" "$out"
fi

# ==================================================================== #
echo "== make.cmd: defines and automatic variables =="
# ==================================================================== #

cat << 'EOF' > testvars.mk
PIP=a:pip

all: bundle.out

bundle.out: testp1.out testp2.out
	$(PIP) $@=testp1.out,testp2.out

testp1.out: testp1.txt
	$(PIP) $@=$<

testp2.out: testp2.txt \
	testp2.inc
	$(PIP) $@=$*
EOF

echo "p1" > testp1.txt
echo "p2" > testp2.txt
echo "inc" > testp2.inc

run make.cmd -f testvars.mk -t testts -n
expected_vars="a:pip testp1.out=testp1.txt
a:pip testp2.out=testp2.txt testp2.inc
a:pip bundle.out=testp1.out,testp2.out"

if [ "$out" = "$expected_vars" ]; then
	pass "make.cmd" "expands variables, automatic macros and line continuations"
else
	blame "make.cmd" "expands variables, automatic macros and line continuations" "-f testvars.mk -n" "$expected_vars" "$out"
fi

rm -f testp1.txt testp2.txt testp2.inc

# ==================================================================== #
echo "== make.cmd: dmnd dependencies =="
# ==================================================================== #

echo "int a(){return 0;}" > testa.c
echo "int b(){return 0;}" > testb.c
echo "#define COMMON 1"   > testcomm.h

cat << 'EOF' > testdmnd.mk
all: testprog.cmd

testprog.cmd: testa.o testb.o
	link testprog.cmd testa.o testb.o

testa.o: testa.c testcomm.h
	cc testa.c

testb.o: testb.c testcomm.h
	cc testb.c
EOF

run make.cmd -f testdmnd.mk -t testts -n
expected_dmnd="cc testa.c
cc testb.c
link testprog.cmd testa.o testb.o"

if [ "$out" = "$expected_dmnd" ]; then
	pass "make.cmd" "dmnd DAG evaluates correctly"
else
	blame "make.cmd" "dmnd DAG evaluates correctly" "-f testdmnd.mk -n" "$expected_dmnd" "$out"
fi


# ==================================================================== #
echo "== make.cmd: error cases =="
# ==================================================================== #

# 9. Missing prerequisite with no rule
cat << 'EOF' > testmiss.mk
app: missing.c
	cc missing.c
EOF

run make.cmd -f testmiss.mk -n app
if printf '%s\n' "$diag" | grep -qiF "No rule to make target"; then
	pass "make.cmd" "reports error on missing source file"
else
	blame "make.cmd" "reports error on missing source file" "-f testmiss.mk -n app" "ERR: No rule to make target 'missing.c'" "$diag"
fi

# 10. Circular dependency detection
cat << 'EOF' > testcycl.mk
a: b
	cc a
b: a
	cc b
EOF

run make.cmd -f testcycl.mk -n a
if printf '%s\n' "$diag" | grep -qiF "Circular dependency"; then
	pass "make.cmd" "detects circular dependency cycle"
else
	blame "make.cmd" "detects circular dependency cycle" "-f testcycl.mk -n a" "ERR: Circular dependency detected for 'a'" "$diag"
fi

# ==================================================================== #
echo
if [ $fail -eq 0 ]; then
	echo "All regression tests passed successfully!"
else
	echo "SOME TESTS FAILED!"
fi
exit $fail
