/*
 * state.c -- CP/M-86 Make State Management on Disk
 *
 * Licensed under the MIT license. See LICENSE file in the project root.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "state.h"

#ifdef __STDC__
extern int unlink(const char *path);
#else
extern int unlink();
#endif

/* Trim trailing CR, LF, and whitespace */
static void state_trim(char *s)
{
    int len = strlen(s);
    while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n' || s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[--len] = '\0';
    }
}

int state_init(make_state_t *st, char *makefile, char *dbname, char *main_target)
{
    if (!st)
        return -1;

    memset(st, 0, sizeof(make_state_t));

    if (makefile) {
        strncpy(st->makefile, makefile, STATE_NAME_LEN - 1);
    } else {
        strcpy(st->makefile, "makefile");
    }

    if (dbname) {
        strncpy(st->dbname, dbname, STATE_NAME_LEN - 1);
    } else {
        strcpy(st->dbname, "ts");
    }

    if (main_target) {
        strncpy(st->main_target, main_target, STATE_NAME_LEN - 1);
    }

    return 0;
}

int state_push(make_state_t *st, char *target)
{
    if (!st || !target || st->target_count >= STATE_MAX_TARGETS)
        return -1;

    strncpy(st->targets[st->target_count], target, STATE_NAME_LEN - 1);
    st->targets[st->target_count][STATE_NAME_LEN - 1] = '\0';
    st->target_count++;
    return 0;
}

int state_pop(make_state_t *st, char *target_buf,
    int buf_size)
{
    int i;

    if (!st || st->target_count <= 0)
        return -1;

    if (target_buf && buf_size > 0) {
        strncpy(target_buf, st->targets[0], buf_size - 1);
        target_buf[buf_size - 1] = '\0';
    }

    /* Shift remaining targets forward (FIFO queue) */
    for (i = 0; i < st->target_count - 1; i++) {
        strncpy(st->targets[i], st->targets[i + 1], STATE_NAME_LEN - 1);
        st->targets[i][STATE_NAME_LEN - 1] = '\0';
    }
    st->target_count--;
    st->targets[st->target_count][0] = '\0';
    return 0;
}

char *state_peek(make_state_t *st)
{
    if (!st || st->target_count <= 0)
        return NULL;
    return st->targets[0];
}

int state_count(make_state_t *st)
{
    if (!st)
        return 0;
    return st->target_count;
}

void state_set_current(make_state_t *st, char *target)
{
    if (!st)
        return;
    if (target) {
        strncpy(st->current_target, target, STATE_NAME_LEN - 1);
        st->current_target[STATE_NAME_LEN - 1] = '\0';
    } else {
        st->current_target[0] = '\0';
    }
}

char *state_get_current(make_state_t *st)
{
    if (!st)
        return "";
    return st->current_target;
}

int state_save(make_state_t *st, char *filepath)
{
    FILE *fp;
    char *path;
    int i, failed;

    if (!st)
        return -1;

    path = filepath ? filepath : STATE_DEFAULT_FILE;
    fp = fopen(path, "w");
    if (!fp)
        return -1;

    fprintf(fp, "version=2\n");
    fprintf(fp, "makefile=%s\n", st->makefile);
    fprintf(fp, "dbname=%s\n", st->dbname);
    fprintf(fp, "main_target=%s\n", st->main_target);
    fprintf(fp, "current_target=%s\n", st->current_target);
    fprintf(fp, "backup=%s\n", st->backup);
    fprintf(fp, "count=%d\n", st->target_count);

    for (i = 0; i < st->target_count; i++) {
        fprintf(fp, "target=%s\n", st->targets[i]);
    }

    fprintf(fp, "end=make\n");
    failed = ferror(fp);
    if (fflush(fp) != 0) failed = 1;
    if (fclose(fp) != 0) failed = 1;
    if (failed) {
        fprintf(stderr, "ERR: Cannot write state '%s'\n", path);
        return -1;
    }
    return 0;
}

/* Reject partial/legacy state: never silently skip an interrupted recipe. */
int state_load(make_state_t *st, char *filepath)
{
    FILE *fp;
    char buf[128];
    char *value;
    char *dest;
    int count = -1, fields = 0, bit, ended = 0, failed = 0;
    int n;

    if (!st) return -1;
    fp = fopen(filepath ? filepath : STATE_DEFAULT_FILE, "r");
    if (!fp) return -1;
    memset(st, 0, sizeof(make_state_t));
    while (fgets(buf, sizeof(buf), fp)) {
        state_trim(buf);
        if (!*buf || (unsigned char)buf[0] == 0x1a) continue;
        if (ended) { failed = 1; break; }
        value = strchr(buf, '=');
        if (!value) { failed = 1; break; }
        *value++ = '\0';
        dest = NULL;
        bit = 0;
        if (!strcmp(buf, "version")) {
            bit = 1;
            if (strcmp(value, "2")) failed = 1;
        } else if (!strcmp(buf, "makefile")) { bit = 2; dest = st->makefile; }
        else if (!strcmp(buf, "dbname")) { bit = 4; dest = st->dbname; }
        else if (!strcmp(buf, "main_target")) { bit = 8; dest = st->main_target; }
        else if (!strcmp(buf, "current_target")) { bit = 16; dest = st->current_target; }
        else if (!strcmp(buf, "backup")) { bit = 32; dest = st->backup; }
        else if (!strcmp(buf, "count")) {
            bit = 64;
            n = 0;
            if (!*value) failed = 1;
            while (value[n]) {
                if (value[n] < '0' || value[n] > '9' || n >= 2) failed = 1;
                n++;
            }
            count = atoi(value);
            if (count < 0 || count > STATE_MAX_TARGETS) failed = 1;
        } else if (!strcmp(buf, "target")) {
            if (st->target_count >= STATE_MAX_TARGETS || !*value) failed = 1;
            else dest = st->targets[st->target_count++];
        } else if (!strcmp(buf, "end")) {
            if (strcmp(value, "make")) failed = 1;
            ended = 1;
        } else failed = 1;
        if (bit && (fields & bit)) failed = 1;
        fields |= bit;
        if (dest) {
            if (strlen(value) >= STATE_NAME_LEN) failed = 1;
            else strcpy(dest, value);
        }
        if (failed) break;
    }
    if (ferror(fp)) failed = 1;
    if (fclose(fp) != 0) failed = 1;
    if (failed || !ended || fields != 127 || count != st->target_count ||
        !*st->makefile || !*st->dbname || !*st->main_target ||
        (*st->backup && !*st->current_target)) return -1;
    return 0;
}

int state_exists(char *filepath)
{
    FILE *fp;
    char *path;

    path = filepath ? filepath : STATE_DEFAULT_FILE;
    fp = fopen(path, "r");
    if (!fp)
        return 0;
    fclose(fp);
    return 1;
}

int state_delete(char *filepath)
{
    char *path;

    path = filepath ? filepath : STATE_DEFAULT_FILE;
    if (!state_exists(path)) return 0;
    return unlink(path);
}
