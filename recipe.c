/* Output validation shared by submit and concurrent execution. */
#include <stdio.h>
#include <string.h>
#include "make.h"
#include "util.h"

#ifdef __STDC__
extern int unlink(const char *path);
#else
extern int unlink();
#endif

/* Keep the backup on the same drive/user as the target. */
static int backup_name(char *name, char *backup)
{
    char *colon = strrchr(name, ':');
    char *slash = strrchr(name, '/');
    int n = colon ? colon - name + 1 : (slash ? slash - name + 1 : 0);
    if (n + 9 > STATE_NAME_LEN) return -1;
    memcpy(backup, name, n);
    strcpy(backup + n, "make.bak");
    return 0;
}

int make_prepare_target(make_state_t *st, make_target_t *t)
{
    char backup[STATE_NAME_LEN];
    state_set_current(st, t->name);
    st->backup[0] = '\0';
    if (t->is_phony || !t->cmd_count) return 0;
    if (backup_name(t->name, backup) < 0) return -1;
    if (!strcmp(t->name, backup) || !strcmp(t->name, STATE_DEFAULT_FILE) ||
        make_file_exists(backup)) {
        fprintf(stderr, "ERR: Reserved backup '%s' exists; recover it first\n", backup);
        return -1;
    }
    /* A failed rebuild must remain out of date even if a partial file exists. */
    if (make_set_timestamp(st->dbname, t->name, 0L) < 0) {
        fprintf(stderr, "ERR: Cannot invalidate timestamp for '%s'\n", t->name);
        return -1;
    }
    if (make_file_exists(t->name)) {
        if (rename(t->name, backup) != 0) {
            fprintf(stderr, "ERR: Cannot preserve target '%s'\n", t->name);
            return -1;
        }
        strcpy(st->backup, backup);
    }
    return 0;
}

int make_abort_target(make_state_t *st)
{
    char expected[STATE_NAME_LEN];
    /* A failed stamp/cleanup may have written a positive timestamp already. */
    if (*st->current_target &&
        make_set_timestamp(st->dbname, st->current_target, 0L) < 0) {
        fprintf(stderr, "ERR: Cannot invalidate failed target; recovery state retained\n");
        return -1;
    }
    if (*st->backup) {
        if (!*st->current_target || backup_name(st->current_target, expected) < 0)
            return -1;
        if (strcmp(expected, st->backup) || !make_file_exists(st->backup)) {
            fprintf(stderr, "ERR: Invalid or missing output backup; recover manually\n");
            return -1;
        }
        if (make_file_exists(st->current_target) && unlink(st->current_target) != 0)
            return -1;
        if (rename(st->backup, st->current_target) != 0) {
            fprintf(stderr, "ERR: Cannot restore '%s' from '%s'\n",
                    st->current_target, st->backup);
            return -1;
        }
    }
    st->backup[0] = '\0';
    state_set_current(st, NULL);
    return 0;
}

int make_finish_target(make_state_t *st, make_target_t *t)
{
    long now;
    char expected[STATE_NAME_LEN];
    if (*st->backup && (backup_name(t->name, expected) < 0 ||
        strcmp(expected, st->backup) || !make_file_exists(st->backup))) {
        fprintf(stderr, "ERR: Invalid or missing output backup; recover manually\n");
        return -1;
    }
    if (!t->is_phony) {
        if (!make_file_exists(t->name)) {
            fprintf(stderr, "ERR: Target '%s' not created\n", t->name);
            return -1;
        }
        now = epoch_now();
        if (now == EPOCH_ERR) now = 1L;
        if (make_set_timestamp(st->dbname, t->name, now) < 0) {
            fprintf(stderr, "ERR: Cannot timestamp '%s'\n", t->name);
            return -1;
        }
    }
    if (*st->backup && unlink(st->backup) != 0) {
        fprintf(stderr, "ERR: Cannot remove backup '%s'\n", st->backup);
        return -1;
    }
    st->backup[0] = '\0';
    state_set_current(st, NULL);
    return 0;
}
