/*
 * state.c -- CP/M-86 Make State Management on Disk
 *
 * Licensed under the MIT license. See LICENSE file in the project root.
 */
#include <stdio.h>
#include <string.h>
#include "state.h"

#ifndef __STDC__
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
    int i;

    if (!st)
        return -1;

    path = filepath ? filepath : STATE_DEFAULT_FILE;
    fp = fopen(path, "w");
    if (!fp)
        return -1;

    fprintf(fp, "makefile=%s\n", st->makefile);
    fprintf(fp, "dbname=%s\n", st->dbname);
    fprintf(fp, "main_target=%s\n", st->main_target);
    fprintf(fp, "current_target=%s\n", st->current_target);
    fprintf(fp, "count=%d\n", st->target_count);

    for (i = 0; i < st->target_count; i++) {
        fprintf(fp, "target=%s\n", st->targets[i]);
    }

    fclose(fp);
    return 0;
}

int state_load(make_state_t *st, char *filepath)
{
    FILE *fp;
    char *path;
    char buf[128];
    int count;

    if (!st)
        return -1;

    path = filepath ? filepath : STATE_DEFAULT_FILE;
    fp = fopen(path, "r");
    if (!fp)
        return -1;

    memset(st, 0, sizeof(make_state_t));

    while (fgets(buf, sizeof(buf), fp)) {
        state_trim(buf);
        if (strncmp(buf, "makefile=", 9) == 0) {
            strncpy(st->makefile, buf + 9, STATE_NAME_LEN - 1);
        } else if (strncmp(buf, "dbname=", 7) == 0) {
            strncpy(st->dbname, buf + 7, STATE_NAME_LEN - 1);
        } else if (strncmp(buf, "main_target=", 12) == 0) {
            strncpy(st->main_target, buf + 12, STATE_NAME_LEN - 1);
        } else if (strncmp(buf, "current_target=", 15) == 0) {
            strncpy(st->current_target, buf + 15, STATE_NAME_LEN - 1);
        } else if (strncmp(buf, "count=", 6) == 0) {
            count = atoi(buf + 6);
        } else if (strncmp(buf, "target=", 7) == 0) {
            if (st->target_count < STATE_MAX_TARGETS) {
                strncpy(st->targets[st->target_count], buf + 7, STATE_NAME_LEN - 1);
                st->targets[st->target_count][STATE_NAME_LEN - 1] = '\0';
                st->target_count++;
            }
        }
    }

    fclose(fp);
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
    unlink(path);
    return 0;
}
