/*
 * ts -- CP/M-86 Timestamp Management Utility
 *
 * Usage:
 *   ts [-t db] <filepattern...>        Query timestamp for files (0 if not found)
 *   ts [-t db] -s <file> [timestamp]  Set file timestamp (system epoch or explicit value)
 *   ts [-t db] -d <filepattern...>      Delete file timestamp records
 *
 * Licensed under the MIT license. See LICENSE file in the project root.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include "ndbm.h"
#include "util.h"

#define TS_MAX_KEYS 128
#define TS_KEY_LEN  64

#ifndef __STDC__
extern int bdos();
#endif

/* Convert string to lowercase in place */
static void ts_strlower(char *s)
{
    for (; *s; s++)
        *s = tolower(*s);
}

/* Simple wildcard pattern matcher (* and ?, case-insensitive) */
static int ts_match(char *pat, char *str)
{
    while (*pat) {
        if (*pat == '*') {
            pat++;
            if (!*pat)
                return 1;
            while (*str) {
                if (ts_match(pat, str))
                    return 1;
                str++;
            }
            return 0;
        }
        if (!*str)
            return 0;
        if (*pat != '?' && tolower(*pat) != tolower(*str))
            return 0;
        pat++;
        str++;
    }
    return *str == '\0';
}

/* Check if string contains wildcard characters */
static int ts_has_wildcards(char *s)
{
    for (; *s; s++) {
        if (*s == '*' || *s == '?')
            return 1;
    }
    return 0;
}

static void ts_usage()
{
    fprintf(stderr, "ERR: Invalid command line\n");
    fprintf(stderr, "INF: ts [-t db] <filepattern...>\n");
    fprintf(stderr, "     - query timestamp for files (0 if not found)\n");
    fprintf(stderr, "     ts [-t db] -s <file> [timestamp]\n");
    fprintf(stderr, "     - set file timestamp (epoch or explicit value)\n");
    fprintf(stderr, "     ts [-t db] -d <filepattern...>\n");
    fprintf(stderr, "     - delete timestamp for file patterns\n");
    fprintf(stderr, "     ts [-t db] -l\n");
    fprintf(stderr, "     - list all recorded file timestamps\n");
    exit(1);
}

/* Make a datum from a NUL-terminated string */
static datum ts_str_datum(char *s)
{
    datum d;
    d.dptr  = s;
    d.dsize = strlen(s) + 1;
    return d;
}

/* Open db read-write, creating .dir/.pag files if absent */
static DBM *ts_open_rw(char *name)
{
    DBM *db;
    char path[128];
    int fd;

    /* Create .dir if missing */
    strcpy(path, name);
    strcat(path, ".dir");
    fd = open(path, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        fprintf(stderr, "ERR: Cannot open/create %s\n", path);
        exit(1);
    }
    close(fd);

    /* Create .pag if missing */
    strcpy(path, name);
    strcat(path, ".pag");
    fd = open(path, O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        fprintf(stderr, "ERR: Cannot open/create %s\n", path);
        exit(1);
    }
    close(fd);

    db = dbm_open(name, O_RDWR, 0666);
    if (!db) {
        fprintf(stderr, "ERR: Cannot open database '%s'\n", name);
        exit(1);
    }
    return db;
}

/* Open db read-only, return NULL if missing */
static DBM *ts_open_ro(char *name)
{
    return dbm_open(name, O_RDONLY, 0);
}

/* Query timestamps for one or more patterns */
static int ts_query(char *dbname, char *patterns[], int count)
{
    DBM *db;
    datum key, val;
    int i;
    int matched;

    db = ts_open_ro(dbname);

    for (i = 0; i < count; i++) {
        char pat[TS_KEY_LEN];
        strncpy(pat, patterns[i], sizeof(pat) - 1);
        pat[sizeof(pat) - 1] = '\0';
        ts_strlower(pat);

        if (ts_has_wildcards(pat)) {
            matched = 0;
            if (db) {
                key = dbm_firstkey(db);
                while (key.dptr != NULL) {
                    if (ts_match(pat, key.dptr)) {
                        val = dbm_fetch(db, key);
                        if (val.dptr != NULL) {
                            printf("%s: %s\n", key.dptr, val.dptr);
                            matched++;
                        }
                    }
                    key = dbm_nextkey(db);
                }
            }
            if (matched == 0) {
                if (count == 1)
                    printf("0\n");
                else
                    printf("%s: 0\n", pat);
            }
        } else {
            /* Exact key query */
            val.dptr = NULL;
            if (db) {
                key = ts_str_datum(pat);
                val = dbm_fetch(db, key);
            }
            if (val.dptr != NULL) {
                if (count == 1)
                    printf("%s\n", val.dptr);
                else
                    printf("%s: %s\n", pat, val.dptr);
            } else {
                if (count == 1)
                    printf("0\n");
                else
                    printf("%s: 0\n", pat);
            }
        }
    }

    if (db)
        dbm_close(db);

    return 0;
}

/* Set timestamp for a file */
static int ts_set(char *dbname, char *file, char *epoch_val)
{
    DBM *db;
    datum key, val;
    char buf[32];
    char key_buf[TS_KEY_LEN];
    long now;

    if (epoch_val != NULL) {
        /* Explicit timestamp provided */
        strncpy(buf, epoch_val, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
    } else {

        now = epoch_now();
        if (now == EPOCH_ERR) {
            fprintf(stderr, "ERR: Cannot retrieve system date/time\n");
            return 1;
        }
        sprintf(buf, "%ld", now);
    }

    strncpy(key_buf, file, sizeof(key_buf) - 1);
    key_buf[sizeof(key_buf) - 1] = '\0';
    ts_strlower(key_buf);

    db = ts_open_rw(dbname);
    key = ts_str_datum(key_buf);
    val = ts_str_datum(buf);

    if (dbm_store(db, key, val, DBM_REPLACE) < 0) {
        fprintf(stderr, "ERR: Failed to store timestamp for '%s'\n", key_buf);
        dbm_close(db);
        return 1;
    }

    dbm_close(db);
    return 0;
}

/* Delete timestamps matching one or more patterns */
static int ts_del(char *dbname, char *patterns[], int count)
{
    DBM *db;
    datum key;
    char keys_to_del[TS_MAX_KEYS][TS_KEY_LEN];
    int nkeys;
    int i;
    int k;

    db = ts_open_rw(dbname);

    for (i = 0; i < count; i++) {
        char pat[TS_KEY_LEN];
        strncpy(pat, patterns[i], sizeof(pat) - 1);
        pat[sizeof(pat) - 1] = '\0';
        ts_strlower(pat);

        if (ts_has_wildcards(pat)) {
            /* Collect matching keys first, then delete */
            nkeys = 0;
            key = dbm_firstkey(db);
            while (key.dptr != NULL && nkeys < TS_MAX_KEYS) {
                if (ts_match(pat, key.dptr)) {
                    strncpy(keys_to_del[nkeys], key.dptr, TS_KEY_LEN - 1);
                    keys_to_del[nkeys][TS_KEY_LEN - 1] = '\0';
                    nkeys++;
                }
                key = dbm_nextkey(db);
            }

            for (k = 0; k < nkeys; k++) {
                key = ts_str_datum(keys_to_del[k]);
                dbm_delete(db, key);
            }
        } else {
            key = ts_str_datum(pat);
            dbm_delete(db, key);
        }
    }

    dbm_close(db);
    return 0;
}

/* List all timestamps recorded in the database */
static int ts_list(char *dbname)
{
    DBM *db;
    datum key, val;

    db = ts_open_ro(dbname);
    if (!db)
        return 0;

    key = dbm_firstkey(db);
    while (key.dptr != NULL) {
        val = dbm_fetch(db, key);
        if (val.dptr != NULL) {
            printf("%s: %s\n", key.dptr, val.dptr);
        }
        key = dbm_nextkey(db);
    }

    dbm_close(db);
    return 0;
}

int main(int argc, char *argv[])
{
    char *dbname;
    int argi;
    char *action;

    dbname = "ts";
    argi = 1;

    /* Parse optional -t <db> */
    if (argi < argc && (strcmp(argv[argi], "-t") == 0 || strcmp(argv[argi], "-T") == 0)) {
        argi++;
        if (argi >= argc)
            ts_usage();
        dbname = argv[argi++];
    }

    if (argi >= argc)
        ts_usage();

    action = argv[argi];

    if (strcmp(action, "-s") == 0 || strcmp(action, "-S") == 0) {
        /* Set timestamp: ts [-t db] -s <file> [timestamp] */
        char *file;
        char *epoch_val;

        argi++;
        if (argi >= argc)
            ts_usage();
        file = argv[argi++];
        epoch_val = (argi < argc) ? argv[argi++] : NULL;

        return ts_set(dbname, file, epoch_val);

    } else if (strcmp(action, "-d") == 0 || strcmp(action, "-D") == 0) {
        /* Delete timestamps: ts [-t db] -d <filepattern...> */
        argi++;
        if (argi >= argc)
            ts_usage();

        return ts_del(dbname, &argv[argi], argc - argi);

    } else if (strcmp(action, "-l") == 0 || strcmp(action, "-L") == 0) {
        /* List all timestamps: ts [-t db] -l */
        return ts_list(dbname);

    } else if (action[0] == '-') {
        ts_usage();
    } else {
        /* Query timestamps: ts [-t db] <filepattern...> */
        return ts_query(dbname, &argv[argi], argc - argi);
    }

    return 0;
}
