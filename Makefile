CC      = aztec42_cc
AS		= aztec42_as
LD      = aztec42_link
LIB     = aztec42_lib
STRIP   = aztec42_sqz
CFLAGS  = -I. +F +0
CPM_LDFLAGS = -lc86
DOS_LDFLAGS = -lc

all: ndbm.lib util.lib ts.cmd make.cmd

# --------------------------------------------------------------------
# Binaries
# --------------------------------------------------------------------
ts.cmd: ts.o ndbm.lib util.lib
	$(LD) -o $@ ts.o ndbm.lib util.lib $(CPM_LDFLAGS)

make.cmd: make.o state.o ndbm.lib util.lib
	$(LD) -o $@ make.o state.o ndbm.lib util.lib $(CPM_LDFLAGS)

prepoch.cmd: prepoch.o util.lib
	$(LD) -o $@ prepoch.o util.lib $(CPM_LDFLAGS)

# --------------------------------------------------------------------
# Binary zip
# --------------------------------------------------------------------
DIST_CMD  = ts.cmd make.cmd
DIST_DAT  = part1.txt part2.txt
DIST_MAK  = test.mk

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
cpmtest.img: ts.cmd make.cmd test.mk part1.txt part2.txt
	cp cpmbase.img cpmtest.img
	cpmcp -f ibmpc-514ss cpmtest.img ts.cmd 0:
	cpmcp -f ibmpc-514ss cpmtest.img make.cmd 0:
	cpmcp -f ibmpc-514ss cpmtest.img test.mk 0:makefile
	cpmcp -f ibmpc-514ss cpmtest.img part1.txt 0:
	cpmcp -f ibmpc-514ss cpmtest.img part2.txt 0:
	cpmls -F -f ibmpc-514ss cpmtest.img

test-local:
	sh ./test.sh

test: 
	make test-local
	make cpm86test

cpm86test: cpmtest.img
	./cpm86


