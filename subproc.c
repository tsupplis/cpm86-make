#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "make.h"
#include "util.h"
#include "sub.h"

int sub_process(make_state_t *state, make_graph_t *graph, char *makefile,
                char *dbname, char *target) {
    int k;
    char next_tgt[STATE_NAME_LEN];
    make_target_t *t;
    char resume_cmd[128];

    /* Pop the first target to build */
    if (state_pop(state, next_tgt, sizeof(next_tgt)) < 0) {
        fprintf(stdout, "INF: Build complete.\n");
        state_delete(STATE_DEFAULT_FILE);
        return 0;
    }

    t = make_find_target(graph, next_tgt);
    if (!t || t->cmd_count == 0) {
        /* No commands for this target (e.g. meta/phony target).
           Only record a timestamp for a non-phony target that exists on disk.
         */
        if (!(t && t->is_phony)) {
            if (make_file_exists(next_tgt)) {
                long now = epoch_now();
                if (now == EPOCH_ERR)
                    now= 1L;
                make_set_timestamp(dbname, next_tgt, now);
            } else {
                fprintf(stderr, "ERR: Target '%s' not created\n", next_tgt);
                state_delete(STATE_DEFAULT_FILE);
                return 1;
            }
        }
        state_save(state, STATE_DEFAULT_FILE);

        /* If there are more targets in stack, re-run make */
        if (state_count(state) > 0) {
            sprintf(resume_cmd, "make -f %s -t %s %s%s", makefile, dbname,
                    is_make_verbose() ? "-v " : "", target);
            sub_open(SUB_APPEND);
            sub_append(resume_cmd);
            sub_close();
         } else {
            fprintf(stdout, "INF: Build complete.\n");
            state_delete(STATE_DEFAULT_FILE);
        }
        return 0;
    }

    /* Save state with current target being built */
    state_set_current(state, next_tgt);
    state_save(state, STATE_DEFAULT_FILE);

    /* Build $$$.SUB:
       CCP executes records LIFO (last appended first).
       Pushed on top of any existing $$$.SUB (SUB_APPEND), so a pending
       batch is preserved and resumes after this step.
       1. First push resume make command (will run last in this step)
       2. Push recipe commands in reverse order (cmd[N-1] down to cmd[0]) */
    if (is_make_verbose()) {
        printf("INF: Batching target '%s' into $$$.SUB (%d commands)\n",
               next_tgt, t->cmd_count);
        for (k = 0; k < t->cmd_count; k++) {
            printf("INF:   + %s\n", t->cmds[k].text);
        }
    }

    sub_open(SUB_APPEND);

    sprintf(resume_cmd, "make -f %s -t %s %s%s", makefile, dbname,
            is_make_verbose() ? "-v " : "", target);
    sub_append(resume_cmd);

    for (k = t->cmd_count - 1; k >= 0; k--) {
        sub_append(t->cmds[k].text);
    }

    sub_close();
    return 0;
}
