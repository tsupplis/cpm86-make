/*
 * make.h -- CP/M-86 Make Tool Header
 *
 * Licensed under the MIT license. See LICENSE file in the project root.
 */
#ifndef MAKE_H
#define MAKE_H

#include "state.h"
#include "ndbm.h"

#define MAKE_MAX_TARGETS 32
#define MAKE_MAX_DEPS    16
#define MAKE_MAX_CMDS    16
#define MAKE_MAX_VARS    32
#define MAKE_LINE_LEN    128
#define MAKE_NAME_LEN    64
#define MAKE_VAR_NAME_LEN 32
#define MAKE_VAR_VAL_LEN  128

typedef struct _make_cmd_t {
    char text[MAKE_LINE_LEN];
} make_cmd_t;

typedef struct _make_var_t {
    char name[MAKE_VAR_NAME_LEN];
    char value[MAKE_VAR_VAL_LEN];
} make_var_t;

typedef struct _make_target_t {
    char name[MAKE_NAME_LEN];
    int  dep_count;
    char deps[MAKE_MAX_DEPS][MAKE_NAME_LEN];
    int  cmd_count;
    make_cmd_t cmds[MAKE_MAX_CMDS];
    int  visited;   /* 0 = unvisited, 1 = visiting (cycle), 2 = resolved */
    int  needed;    /* 1 = out of date, needs build */
    int  is_phony;  /* 1 = phony/action target, file existence not required */
    long timestamp; /* cached timestamp from ts.db */
} make_target_t;

typedef struct _make_graph_t {
    int target_count;
    make_target_t targets[MAKE_MAX_TARGETS];
    char first_target[MAKE_NAME_LEN];
    int var_count;
    make_var_t vars[MAKE_MAX_VARS];
} make_graph_t;

#ifndef __STDC__
int  make_parse();
int  make_build_dag();
int  make_file_exists();
long make_get_timestamp();
#else
int  make_parse(make_graph_t *g, char *filepath);
int  make_build_dag(make_graph_t *g, char *target_name, char *dbname, make_state_t *st);
int  make_file_exists(char *filename);
long make_get_timestamp(char *dbname, char *filename);
#endif

#endif
