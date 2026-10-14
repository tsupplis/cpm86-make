/*
 * make -- CP/M-86 Make Build Automation Tool
 *
 * Usage:
 *   make [-f makefile] [-t db] [-n] [target]
 *
 * Licensed under the MIT license. See LICENSE file in the project root.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include "make.h"
#include "epoch.h"
#include "sub.h"

#ifndef __STDC__
extern int bdos();
extern int unlink();
#endif

static int make_verbose = 0;
static int make_always_build = 0;

/* Convert string to lowercase in place */
static void make_strlower(char *s)
{
    for (; *s; s++)
        *s = tolower(*s);
}

/* Extract next whitespace-delimited token */
static char *make_next_token(char **p)
{
    char *start;
    char *s;

    if (!p || !*p)
        return NULL;

    s = *p;
    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '\0') {
        *p = s;
        return NULL;
    }
    start = s;
    while (*s != '\0' && *s != ' ' && *s != '\t')
        s++;
    if (*s != '\0') {
        *s = '\0';
        *p = s + 1;
    } else {
        *p = s;
    }
    return start;
}

/* Trim leading whitespace */
static char *make_skip_ws(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

/* Trim trailing whitespace, newlines, and CP/M EOF (^Z) */
static void make_trim_trailing(char *s)
{
    int len = strlen(s);
    while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n' || s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == 0x1A)) {
        s[--len] = '\0';
    }
}

static void make_usage()
{
    fprintf(stderr, "ERR: Invalid command line\n");
    fprintf(stderr, "INF: make [-f makefile] [-t db] [-a] [-r] [-n] [-v] [target]\n");
    fprintf(stderr, "     -f <file>  alternate makefile (default: makefile)\n");
    fprintf(stderr, "     -t <db>    alternate timestamp db (default: ts)\n");
    fprintf(stderr, "     -a         unconditional rebuild (force all targets)\n");
    fprintf(stderr, "     -r         reset build state (clear make.sta)\n");
    fprintf(stderr, "     -n         dry run / test mode (print commands only)\n");
    fprintf(stderr, "     -v         verbose mode (trace evaluation and batching)\n");
    exit(1);
}

/* Check if a file exists on disk */
int make_file_exists(char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp)
        return 0;
    fclose(fp);
    return 1;
}

/* Retrieve timestamp for a file from ndbm */
long make_get_timestamp(char *dbname, char *filename)
{
    DBM *db;
    datum key, val;
    char key_buf[MAKE_NAME_LEN];
    long ts = 0L;

    strncpy(key_buf, filename, sizeof(key_buf) - 1);
    key_buf[sizeof(key_buf) - 1] = '\0';
    make_strlower(key_buf);

    db = dbm_open(dbname, O_RDONLY, 0);
    if (!db)
        return 0L;

    key.dptr = key_buf;
    key.dsize = strlen(key_buf) + 1;
    val = dbm_fetch(db, key);
    if (val.dptr != NULL) {
        ts = atol(val.dptr);
    }

    dbm_close(db);
    return ts;
}

/* Store timestamp for a file into ndbm */
static int make_set_timestamp(char *dbname, char *filename, long ts_val)
{
    DBM *db;
    datum key, val;
    char key_buf[MAKE_NAME_LEN];
    char val_buf[32];
    int fd;
    char path[128];

    /* Ensure .dir and .pag exist */
    strcpy(path, dbname);
    strcat(path, ".dir");
    fd = open(path, O_RDWR | O_CREAT, 0666);
    if (fd >= 0) close(fd);

    strcpy(path, dbname);
    strcat(path, ".pag");
    fd = open(path, O_RDWR | O_CREAT, 0666);
    if (fd >= 0) close(fd);

    db = dbm_open(dbname, O_RDWR, 0666);
    if (!db)
        return -1;

    strncpy(key_buf, filename, sizeof(key_buf) - 1);
    key_buf[sizeof(key_buf) - 1] = '\0';
    make_strlower(key_buf);

    sprintf(val_buf, "%ld", ts_val);

    key.dptr = key_buf;
    key.dsize = strlen(key_buf) + 1;
    val.dptr = val_buf;
    val.dsize = strlen(val_buf) + 1;

    dbm_store(db, key, val, DBM_REPLACE);
    dbm_close(db);
    return 0;
}

/* Find target by name in the graph */
static make_target_t *make_find_target(make_graph_t *g, char *name)
{
    char lower_name[MAKE_NAME_LEN];
    int i;

    if (!g || !name || !*name)
        return NULL;

    strncpy(lower_name, name, MAKE_NAME_LEN - 1);
    lower_name[MAKE_NAME_LEN - 1] = '\0';
    make_strlower(lower_name);

    for (i = 0; i < g->target_count; i++) {
        if (strcmp(g->targets[i].name, lower_name) == 0)
            return &g->targets[i];
    }
    return NULL;
}

/* Set or update a variable definition */
static void make_set_var(make_graph_t *g, char *name, char *val)
{
    int i;
    char lower_name[MAKE_VAR_NAME_LEN];

    if (!g || !name || !*name)
        return;

    strncpy(lower_name, name, sizeof(lower_name) - 1);
    lower_name[sizeof(lower_name) - 1] = '\0';
    make_strlower(lower_name);

    for (i = 0; i < g->var_count; i++) {
        char exist_lower[MAKE_VAR_NAME_LEN];
        strncpy(exist_lower, g->vars[i].name, sizeof(exist_lower) - 1);
        exist_lower[sizeof(exist_lower) - 1] = '\0';
        make_strlower(exist_lower);
        if (strcmp(exist_lower, lower_name) == 0) {
            strncpy(g->vars[i].value, val, MAKE_VAR_VAL_LEN - 1);
            g->vars[i].value[MAKE_VAR_VAL_LEN - 1] = '\0';
            return;
        }
    }

    if (g->var_count < MAKE_MAX_VARS) {
        strncpy(g->vars[g->var_count].name, name, MAKE_VAR_NAME_LEN - 1);
        g->vars[g->var_count].name[MAKE_VAR_NAME_LEN - 1] = '\0';
        strncpy(g->vars[g->var_count].value, val, MAKE_VAR_VAL_LEN - 1);
        g->vars[g->var_count].value[MAKE_VAR_VAL_LEN - 1] = '\0';
        g->var_count++;
    }
}

/* Retrieve variable value by name (case-insensitive) */
static char *make_get_var(make_graph_t *g, char *name)
{
    int i;
    char lower_name[MAKE_VAR_NAME_LEN];

    if (!g || !name || !*name)
        return NULL;

    strncpy(lower_name, name, sizeof(lower_name) - 1);
    lower_name[sizeof(lower_name) - 1] = '\0';
    make_strlower(lower_name);

    for (i = 0; i < g->var_count; i++) {
        char exist_lower[MAKE_VAR_NAME_LEN];
        strncpy(exist_lower, g->vars[i].name, sizeof(exist_lower) - 1);
        exist_lower[sizeof(exist_lower) - 1] = '\0';
        make_strlower(exist_lower);
        if (strcmp(exist_lower, lower_name) == 0)
            return g->vars[i].value;
    }
    return NULL;
}

/* Expand variables and automatic macros ($@, $<, $*, $^, $(VAR)) */
static void make_expand_cmd(make_graph_t *g, char *dst, int max_len, char *src, 
    make_target_t *t)
{
    char *p = src;
    int d = 0;
    char all_deps[MAKE_LINE_LEN];
    int i;

    /* Build space-separated list of all dependencies */
    all_deps[0] = '\0';
    if (t) {
        for (i = 0; i < t->dep_count; i++) {
            if (i > 0 && (int)strlen(all_deps) + 1 < (int)sizeof(all_deps))
                strcat(all_deps, " ");
            if ((int)strlen(all_deps) + (int)strlen(t->deps[i]) < (int)sizeof(all_deps))
                strcat(all_deps, t->deps[i]);
        }
    }

    while (*p && d < max_len - 1) {
        if (*p == '$') {
            p++;
            if (*p == '\0') {
                dst[d++] = '$';
                break;
            }
            if (*p == '$') {
                dst[d++] = '$';
                p++;
                continue;
            }
            if (*p == '@') {
                char *sub = (t ? t->name : "");
                while (*sub && d < max_len - 1)
                    dst[d++] = *sub++;
                p++;
                continue;
            }
            if (*p == '<') {
                char *sub = (t && t->dep_count > 0 ? t->deps[0] : "");
                while (*sub && d < max_len - 1)
                    dst[d++] = *sub++;
                p++;
                continue;
            }
            if (*p == '*' || *p == '^') {
                char *sub = all_deps;
                while (*sub && d < max_len - 1)
                    dst[d++] = *sub++;
                p++;
                continue;
            }
            if (*p == '(' || *p == '{') {
                char close_char = (*p == '(') ? ')' : '}';
                char var_name[MAKE_VAR_NAME_LEN];
                int vi = 0;
                char *sub;

                p++;
                while (*p && *p != close_char && vi < (int)sizeof(var_name) - 1) {
                    var_name[vi++] = *p++;
                }
                var_name[vi] = '\0';
                if (*p == close_char) p++;

                sub = make_get_var(g, var_name);
                if (sub) {
                    while (*sub && d < max_len - 1)
                        dst[d++] = *sub++;
                }
                continue;
            }
            /* Single character var, e.g. $X */
            {
                char var_name[2];
                char *sub;
                var_name[0] = *p++;
                var_name[1] = '\0';
                sub = make_get_var(g, var_name);
                if (sub) {
                    while (*sub && d < max_len - 1)
                        dst[d++] = *sub++;
                }
                continue;
            }
        }
        dst[d++] = *p++;
    }
    dst[d] = '\0';
}

/* Read logical line with line continuation support (\ at end) */
static int make_read_logical_line(char *buf, int max_len, FILE *fp)
{
    int total_len = 0;
    buf[0] = '\0';

    while (fgets(buf + total_len, max_len - total_len, fp)) {
        int len;
        make_trim_trailing(buf + total_len);
        len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\\') {
            buf[len - 1] = ' ';
            total_len = len;
            continue;
        }
        return 1;
    }
    return total_len > 0;
}

/* Parse a makefile into the dependency graph */
int make_parse(make_graph_t *g, char *filepath)
{
    FILE *fp;
    char line[MAKE_LINE_LEN];
    make_target_t *cur_target = NULL;

    memset(g, 0, sizeof(make_graph_t));

    fp = fopen(filepath, "r");
    if (!fp) {
        fprintf(stderr, "ERR: Cannot open makefile '%s'\n", filepath);
        return -1;
    }

    while (make_read_logical_line(line, sizeof(line), fp)) {
        char *p;
        char *colon;
        char *eq;

        /* Skip comments */
        p = line;
        while (*p) {
            if (*p == '#') {
                *p = '\0';
                break;
            }
            p++;
        }

        make_trim_trailing(line);

        /* Blank line */
        if (line[0] == '\0')
            continue;

        /* Command line (leading tab or spaces) */
        if (line[0] == '\t' || (line[0] == ' ' && cur_target != NULL)) {
            char *cmd_text;
            if (cur_target == NULL)
                continue;
            cmd_text = make_skip_ws(line);
            if (*cmd_text == '\0')
                continue;
            if (cur_target->cmd_count < MAKE_MAX_CMDS) {
                make_expand_cmd(g, cur_target->cmds[cur_target->cmd_count].text, MAKE_LINE_LEN, cmd_text, cur_target);
                cur_target->cmd_count++;
            }
            continue;
        }

        /* Check for variable definition (NAME = VALUE) */
        eq = strchr(line, '=');
        colon = strchr(line, ':');
        if (eq != NULL && (colon == NULL || eq < colon)) {
            char *var_name;
            char *var_val;

            *eq = '\0';
            var_name = make_skip_ws(line);
            make_trim_trailing(var_name);
            var_val = make_skip_ws(eq + 1);
            make_trim_trailing(var_val);

            make_set_var(g, var_name, var_val);
            cur_target = NULL;
            continue;
        }

        /* Target line (target: dep1 dep2 ...) */
        if (colon != NULL) {
            char *tgt_name;
            char *deps_part;
            char *tok;

            *colon = '\0';
            tgt_name = make_skip_ws(line);
            make_trim_trailing(tgt_name);

            if (*tgt_name == '\0')
                continue;

            /* Check for .PHONY directive */
            if (strcmp(tgt_name, ".phony") == 0 || strcmp(tgt_name, ".PHONY") == 0) {
                deps_part = colon + 1;
                tok = make_next_token(&deps_part);
                while (tok != NULL) {
                    if (*tok != '\0') {
                        make_target_t *pt = make_find_target(g, tok);
                        if (pt) {
                            pt->is_phony = 1;
                        } else if (g->target_count < MAKE_MAX_TARGETS) {
                            pt = &g->targets[g->target_count++];
                            memset(pt, 0, sizeof(make_target_t));
                            strncpy(pt->name, tok, MAKE_NAME_LEN - 1);
                            make_strlower(pt->name);
                            pt->is_phony = 1;
                        }
                    }
                    tok = make_next_token(&deps_part);
                }
                cur_target = NULL;
                continue;
            }

            cur_target = make_find_target(g, tgt_name);
            if (!cur_target) {
                if (g->target_count >= MAKE_MAX_TARGETS) {
                    fprintf(stderr, "WRN: Maximum targets limit (%d) reached\n", MAKE_MAX_TARGETS);
                    break;
                }
                cur_target = &g->targets[g->target_count++];
                memset(cur_target, 0, sizeof(make_target_t));
                strncpy(cur_target->name, tgt_name, MAKE_NAME_LEN - 1);
                cur_target->name[MAKE_NAME_LEN - 1] = '\0';
                make_strlower(cur_target->name);
            }

            if (strcmp(cur_target->name, "clean") == 0 || strcmp(cur_target->name, "all") == 0 || cur_target->name[0] == '.') {
                cur_target->is_phony = 1;
            }

            if (g->first_target[0] == '\0') {
                strncpy(g->first_target, cur_target->name, MAKE_NAME_LEN - 1);
            }

            /* Parse dependencies */
            deps_part = colon + 1;
            tok = make_next_token(&deps_part);
            while (tok != NULL && cur_target->dep_count < MAKE_MAX_DEPS) {
                if (*tok != '\0') {
                    strncpy(cur_target->deps[cur_target->dep_count], tok, MAKE_NAME_LEN - 1);
                    cur_target->deps[cur_target->dep_count][MAKE_NAME_LEN - 1] = '\0';
                    make_strlower(cur_target->deps[cur_target->dep_count]);
                    cur_target->dep_count++;
                }
                tok = make_next_token(&deps_part);
            }
        }
    }

    fclose(fp);
    return 0;
}


/* Recursive DAG traversal and build planning */
static int make_eval_target(make_graph_t *g, char *tgt_name, char *dbname, make_state_t *st)
{
    make_target_t *t;
    int i;
    int exists;
    long target_ts;
    int dep_needed = 0;

    if (!tgt_name || !*tgt_name)
        return 0;

    t = make_find_target(g, tgt_name);
    if (!t) {
        /* No rule to build tgt_name: must exist on disk as a source file */
        if (!make_file_exists(tgt_name)) {
            fprintf(stderr, "ERR: No rule to make target '%s'\n", tgt_name);
            return -1;
        }
        return 0;
    }

    if (t->visited == 1) {
        fprintf(stderr, "ERR: Circular dependency detected for '%s'\n", tgt_name);
        return -1;
    }

    if (t->visited == 2) {
        /* Already evaluated */
        return 0;
    }

    t->visited = 1;
    exists = make_file_exists(t->name);
    target_ts = make_get_timestamp(dbname, t->name);
    t->timestamp = target_ts;

    /* Evaluate prerequisites */
    for (i = 0; i < t->dep_count; i++) {
        long dep_ts;
        make_target_t *dep_t;

        if (make_eval_target(g, t->deps[i], dbname, st) < 0)
            return -1;

        dep_t = make_find_target(g, t->deps[i]);
        if (dep_t && dep_t->needed) {
            dep_needed = 1;
        }

        dep_ts = make_get_timestamp(dbname, t->deps[i]);
        if (dep_ts > target_ts) {
            dep_needed = 1;
        }
    }

    /* Decision logic: target is needed if:
       1. Target is marked phony, OR
       2. Target does not exist on disk, OR
       3. Timestamp in ts.db is 0 (unrecorded), OR
       4. Any dependency was rebuilt (needed), OR
       5. Any dependency is newer than target */
    if (make_always_build || t->is_phony || !exists || target_ts == 0L || dep_needed) {
        t->needed = 1;
    }

    if (make_verbose) {
        if (t->needed) {
            char reason[64];
            if (!exists) strcpy(reason, "file missing");
            else if (target_ts == 0L) strcpy(reason, "no timestamp in ts.db");
            else if (dep_needed) strcpy(reason, "prerequisite newer or rebuilt");
            else strcpy(reason, "out of date");
            printf("INF: Target '%s' is out of date (%s)\n", t->name, reason);
        } else {
            printf("INF: Target '%s' is up to date (ts=%ld)\n", t->name, target_ts);
        }
    }

    t->visited = 2;

    /* Push target to build stack (leaf targets pushed first) */
    if (t->needed) {
        state_push(st, t->name);
    }

    return 0;
}

int make_build_dag(make_graph_t *g, char *target_name, char *dbname, make_state_t *st)
{
    return make_eval_target(g, target_name, dbname, st);
}

int main(int argc, char *argv[])
{
    char *makefile = "makefile";
    char *dbname = "ts";
    char *target = NULL;
    int dry_run = 0;
    int is_resumed = 0;
    int reset_state = 0;
    int argi = 1;
    make_graph_t graph;
    make_state_t state;
    int i, k;

    /* Parse command line options */
    while (argi < argc) {
        if (strcmp(argv[argi], "-f") == 0 || strcmp(argv[argi], "-F") == 0) {
            argi++;
            if (argi >= argc) make_usage();
            makefile = argv[argi++];
        } else if (strcmp(argv[argi], "-t") == 0 || strcmp(argv[argi], "-T") == 0) {
            argi++;
            if (argi >= argc) make_usage();
            dbname = argv[argi++];
        } else if (strcmp(argv[argi], "-n") == 0 || strcmp(argv[argi], "-N") == 0) {
            dry_run = 1;
            argi++;
        } else if (strcmp(argv[argi], "-v") == 0 || strcmp(argv[argi], "-V") == 0) {
            make_verbose = 1;
            argi++;
        } else if (strcmp(argv[argi], "-a") == 0 || strcmp(argv[argi], "-A") == 0 ||
                   strcmp(argv[argi], "-b") == 0 || strcmp(argv[argi], "-B") == 0) {
            make_always_build = 1;
            argi++;
        } else if (strcmp(argv[argi], "-r") == 0 || strcmp(argv[argi], "-R") == 0) {
            reset_state = 1;
            argi++;
        } else if (argv[argi][0] == '-') {
            make_usage();
        } else {
            if (target != NULL) {
                fprintf(stderr, "ERR: Only one target allowed\n");
                make_usage();
            }
            target = argv[argi++];
        }
    }

    /* Guard: BDOS <= 2.2 when running in build mode */
    if (!dry_run) {
        if ((bdos(12, 0) & 0xff) > 0x22) {
            fprintf(stderr, "ERR: Requires CP/M-86 1.1 or compatible (BDOS <= 2.2)\n");
            sub_exit();
            return 1;
        }
    }

    /* Reset state if requested */
    if (reset_state) {
        if (state_exists(STATE_DEFAULT_FILE))
            state_delete(STATE_DEFAULT_FILE);
    }

    /* Parse makefile first */
    if (make_parse(&graph, makefile) < 0) {
        if (state_exists(STATE_DEFAULT_FILE)) 
            state_delete(STATE_DEFAULT_FILE);
        sub_exit();
        return 1;
    }

    /* Check if resuming an active build state */
    if (!dry_run && state_exists(STATE_DEFAULT_FILE)) {
        char *cur;
        if (state_load(&state, STATE_DEFAULT_FILE) == 0) {
            is_resumed = 1;
            cur = state_get_current(&state);
            if (cur && *cur) {
                make_target_t *cur_t = make_find_target(&graph, cur);
                int phony = (cur_t && cur_t->is_phony);

                if (!phony && !make_file_exists(cur)) {
                    fprintf(stderr, "ERR: Target '%s' not created\n", cur);
                    state_delete(STATE_DEFAULT_FILE);
                    sub_exit();
                    return 1;
                }
                if (make_verbose) {
                    fprintf(stdout, "INF: Target '%s' built successfully\n", cur);
                }
                /* Record updated timestamp if not phony */
                if (!phony) {
                    long now = epoch_now();
                    if (now == EPOCH_ERR) now = 1L;
                    make_set_timestamp(state.dbname, cur, now);
                }
                state_set_current(&state, NULL);
            }

            /* Advance to next pending target */
            if (state_count(&state) == 0) {
                fprintf(stdout, "INF: Build complete.\n");
                state_delete(STATE_DEFAULT_FILE);
                sub_exit();
                return 0;
            }

            /* Resumed with remaining targets */
            makefile = state.makefile;
            dbname = state.dbname;
            target = state.main_target;
        }
    }

    if (target == NULL) {
        if (graph.first_target[0] != '\0') {
            target = graph.first_target;
        } else {
            fprintf(stderr, "ERR: No targets in makefile '%s'\n", makefile);
            if (is_resumed) 
                state_delete(STATE_DEFAULT_FILE);
            sub_exit();
            return 1;
        }
    } else {
        make_strlower(target);
    }

    if (!is_resumed) {
        /* Initialize build state and construct dependency DAG */
        state_init(&state, makefile, dbname, target);

        if (make_build_dag(&graph, target, dbname, &state) < 0) {
            sub_exit();
            return 1;
        }

        if (state_count(&state) == 0) {
            fprintf(stdout, "INF: Target '%s' is up to date.\n", target);
            sub_exit();
            return 0;
        }
    }

    /* Dry Run (-n) Mode */
    if (dry_run) {
        for (i = 0; i < state.target_count; i++) {
            make_target_t *t = make_find_target(&graph, state.targets[i]);
            if (t) {
                for (k = 0; k < t->cmd_count; k++) {
                    printf("%s\n", t->cmds[k].text);
                }
            }
        }
        sub_exit();
        return 0;
    }

    /* Batch Execution via $$$.SUB */
    {
        char next_tgt[STATE_NAME_LEN];
        make_target_t *t;
        char resume_cmd[128];

        /* Pop the first target to build */
        if (state_pop(&state, next_tgt, sizeof(next_tgt)) < 0) {
            fprintf(stdout, "INF: Build complete.\n");
            state_delete(STATE_DEFAULT_FILE);
            sub_exit();
            return 0;
        }

        t = make_find_target(&graph, next_tgt);
        if (!t || t->cmd_count == 0) {
            /* No commands for this target (e.g. meta/phony target).
               Only record a timestamp for a non-phony target that exists on disk. */
            if (!(t && t->is_phony) && make_file_exists(next_tgt)) {
                long now = epoch_now();
                if (now == EPOCH_ERR) now = 1L;
                make_set_timestamp(dbname, next_tgt, now);
            }
            state_save(&state, STATE_DEFAULT_FILE);

            /* If there are more targets in stack, re-run make */
            if (state_count(&state) > 0) {
                sprintf(resume_cmd, "make -f %s -t %s %s%s", makefile, dbname, make_verbose ? "-v " : "", target);
                sub_open(SUB_APPEND);
                sub_append(resume_cmd);
                sub_close();
                sub_exit();
            } else {
                fprintf(stdout, "INF: Build complete.\n");
                state_delete(STATE_DEFAULT_FILE);
                sub_exit();
                return 0;
            }
        }

        /* Save state with current target being built */
        state_set_current(&state, next_tgt);
        state_save(&state, STATE_DEFAULT_FILE);

        /* Build $$$.SUB:
           CCP executes records LIFO (last appended first).
           Pushed on top of any existing $$$.SUB (SUB_APPEND), so a pending
           batch is preserved and resumes after this step.
           1. First push resume make command (will run last in this step)
           2. Push recipe commands in reverse order (cmd[N-1] down to cmd[0]) */
        if (make_verbose) {
            printf("INF: Batching target '%s' into $$$.SUB (%d commands)\n", next_tgt, t->cmd_count);
            for (k = 0; k < t->cmd_count; k++) {
                printf("INF:   + %s\n", t->cmds[k].text);
            }
        }

        sub_open(SUB_APPEND);

        sprintf(resume_cmd, "make -f %s -t %s %s%s", makefile, dbname, make_verbose ? "-v " : "", target);
        sub_append(resume_cmd);

        for (k = t->cmd_count - 1; k >= 0; k--) {
            sub_append(t->cmds[k].text);
        }

        sub_close();
    }
    /* Always leave through sub_exit: warm boot so the CCP continues with
       $$$.SUB (pending recipe commands or the caller's submit). */
    sub_exit();
    return 0;
}
