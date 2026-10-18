CC      = aztec42_cc
AS		= aztec42_as
LD      = aztec42_link
LIB     = aztec42_lib
STRIP   = aztec42_sqz
CFLAGS  = -I. +F +0
CPM_LDFLAGS = -lc86
DOS_LDFLAGS = -lc

all: ndbm.lib util.lib ts.cmd make.cmd ccpmexec.cmd

# --------------------------------------------------------------------
# Binaries
# --------------------------------------------------------------------
ts.cmd: ts.o ndbm.lib util.lib
	$(LD) -o $@ ts.o ndbm.lib util.lib $(CPM_LDFLAGS)

make.cmd: make.o state.o recipe.o subproc.o execproc.o ndbm.lib util.lib
	$(LD) -o $@ make.o state.o recipe.o subproc.o execproc.o ndbm.lib util.lib $(CPM_LDFLAGS)

prepoch.cmd: prepoch.o util.lib
	$(LD) -o $@ prepoch.o util.lib $(CPM_LDFLAGS)

ccpmexec.cmd: ccpmexec.o util.lib
	$(LD) -o $@ ccpmexec.o util.lib $(CPM_LDFLAGS)

# Header changes must rebuild every object that shares the graph/state layout.
make.o recipe.o subproc.o execproc.o: make.h state.h
make.o recipe.o execproc.o ts.o: util.h ndbm.h
subproc.o: sub.h
state.o: state.h

# --------------------------------------------------------------------
# Binary zip
# --------------------------------------------------------------------
DIST_CMD  = ts.cmd make.cmd
DIST_DAT  = part1.txt part2.txt
DIST_MAK  = demo.mk

dist: make.zip

make.zip: $(DIST_CMD) $(DIST_DAT) $(DIST_MAK)
	rm -f make.zip
	zip -j make.zip $^

# --------------------------------------------------------------------
# Compile rules
# --------------------------------------------------------------------
%.o: %.c
	$(CC) $(CFLAGS) -o $@ $<
	$(STRIP) $@

# --------------------------------------------------------------------
# Clean
# --------------------------------------------------------------------
clean:
	rm -f *.o make.cmd ts.cmd make.zip

# --------------------------------------------------------------------
# Test image
# --------------------------------------------------------------------
cpmtest.img: ts.cmd make.cmd ccpmexec.cmd demo.mk part1.txt part2.txt startup.0
	cp cpmbase.img cpmtest.img 
	cpmcp -f ibmpc-514ss cpmtest.img ts.cmd 0:
	cpmcp -f ibmpc-514ss cpmtest.img make.cmd 0:
	cpmcp -f ibmpc-514ss cpmtest.img ccpmexec.cmd 0:
	cpmcp -f ibmpc-514ss cpmtest.img startup.0 0:
	cpmcp -f ibmpc-514ss cpmtest.img demo.mk 0:makefile
	cpmcp -f ibmpc-514ss cpmtest.img part1.txt 0:
	cpmcp -f ibmpc-514ss cpmtest.img part2.txt 0:
	cpmls -F -f ibmpc-514ss cpmtest.img

dostest.img: ts.cmd make.cmd ccpmexec.cmd demo.mk part1.txt part2.txt startup.0
	cp dosbase.img dostest.img 
	mcopy -i dostest.img ts.cmd ::TS.CMD
	mcopy -i dostest.img make.cmd ::MAKE.CMD
	mcopy -i dostest.img ccpmexec.cmd ::CCPMEXEC.CMD
	mcopy -i dostest.img startup.0 ::STARTUP.0
	mcopy -i dostest.img demo.mk ::MAKEFILE
	mcopy -i dostest.img part1.txt ::PART1.TXT
	mcopy -i dostest.img part2.txt ::PART2.TXT
	mdir -i dostest.img

test-local: make.cmd ts.cmd
	python3 tests/regression.py

test: 
	make test-local
	make cpmtest

cpmtest: cpmtest.img
	./cpm86

ccpmtest: cpmtest.img
	./ccpm86

dosplustest: dostest.img
	./dosplus
