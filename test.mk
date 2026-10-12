# CP/M-86 PIP-based test makefile
.phony: all clean

PIP=a:pip

all: bundle.out

bundle.out: part1.out part2.out
	$(PIP) $@=part1.out,part2.out

part1.out: part1.txt
	$(PIP) $@=$<

part2.out: part2.txt
	$(PIP) $@=$<

clean:
	era *.out
	ts -d *.out
