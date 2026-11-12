#!/usr/bin/env python3
"""Fault-inject deletion/read/no-progress errors into the production TS code."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <assert.h>
#include <sys/resource.h>
#define getpriority cpm_getpriority
extern int close(int);
#define main ts_main
#include "ts.c"
#undef main
static DBM database;
static int mode, live, closed, deletes;
DBM *dbm_open(char *name,int flags,int perms) { return &database; }
void dbm_close(DBM *db) { closed++; }
int dbm_store(DBM *db,datum key,datum val,int flags) { return 0; }
long epoch_now(void) { return 100L; }
datum dbm_fetch(DBM *db,datum key) {
 datum d;d.dptr=live?"123":NULL;d.dsize=4;
 if(mode==4) { db->dbm_flags|=_DBM_IOERR;d.dptr=NULL; }
 return d;
}
datum dbm_firstkey(DBM *db) {
 datum d;d.dptr=live?"key.c":NULL;d.dsize=6;
 if(mode==3) { db->dbm_flags|=_DBM_IOERR;d.dptr=NULL; }
 return d;
}
datum dbm_nextkey(DBM *db) { datum d;d.dptr=NULL;d.dsize=0;return d; }
int dbm_delete(DBM *db,datum key) {
 deletes++;
 if(mode==1)return -1;
 if(mode!=2)live=0;
 return 0;
}
int main(void) {
 char *wild="*", *exact="key.c";
 for(mode=1;mode<=4;mode++) {
  live=1;closed=deletes=0;database.dbm_flags=0;
  assert(ts_del("probe",mode==4?&exact:&wild,1)==1);
  assert(closed==1 && live && deletes<=1);
 }
 mode=0;live=0;database.dbm_flags=0;
 assert(ts_del("probe",&exact,1)==0);
 live=1;assert(ts_del("probe",&wild,1)==0 && !live);
 puts("PASS: TS deletion errors, iterator errors, no-progress detection and absent keys");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="ts-delete-faults-") as folder:
    work = Path(folder)
    (work / "test.c").write_text(HARNESS)
    subprocess.run([os.environ.get("CC_HOST", "cc"), "-std=gnu89", "-Wno-deprecated-non-prototype", "-I"+str(ROOT),
                    str(work/"test.c"), "-o", str(work/"test")], check=True)
    subprocess.run([str(work/"test")], cwd=work, check=True, timeout=15)
