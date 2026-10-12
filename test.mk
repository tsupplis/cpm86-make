# CP/M-86 PIP-based test makefile
.phony: all clean

all: bundle.out

bundle.out: part1.out part2.out
	a:pip bundle.out=part1.out,part2.out

part1.out: part1.txt
	a:pip part1.out=part1.txt

part2.out: part2.txt
	a:pip part2.out=part2.txt

clean:
	era *.out
	ts -d *.out
