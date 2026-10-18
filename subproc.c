#include <stdio.h>
#include <string.h>
#include "make.h"
#include "sub.h"

int sub_process(make_state_t *state, make_graph_t *graph, char *makefile,
                char *dbname, char *target)
{
    int k, records, opened = 0;
    char next_tgt[STATE_NAME_LEN];
    char resume_cmd[126];
    make_target_t *t;

    /* Consume commandless targets locally rather than creating empty batches. */
    while (state_pop(state, next_tgt, sizeof(next_tgt)) == 0) {
        t = make_find_target(graph, next_tgt);
        if (!t) {
            fprintf(stderr, "ERR: No saved rule for '%s'\n", next_tgt);
            return 1;
        }
        if (!t->cmd_count) {
            state_set_current(state, next_tgt);
            if (make_finish_target(state, t) < 0) goto failed;
            continue;
        }
        /* Check bounds before sprintf or changing the output/state/submit. */
        if (strlen(makefile) + strlen(dbname) + strlen(target) +
            (is_make_verbose() ? 16 : 13) > 125) {
            fprintf(stderr, "ERR: Resume command exceeds CCP limit\n");
            return 1;
        }
        sprintf(resume_cmd, "make -f %s -t %s %s%s", makefile, dbname,
                is_make_verbose() ? "-v " : "", target);
        records = sub_records();
        if (records + t->cmd_count + 1 > 128) {
            fprintf(stderr, "ERR: Insufficient space in $$$.SUB\n");
            return 1;
        }
        for (k = 0; k < t->cmd_count; k++) {
            if (strlen(t->cmds[k]) > 125) {
                fprintf(stderr, "ERR: Recipe exceeds CCP command limit\n");
                return 1;
            }
        }
        if (make_prepare_target(state, t) < 0) return 1;
        if (state_save(state, STATE_DEFAULT_FILE) < 0) goto failed;
        if (sub_open(SUB_APPEND) < 0) goto failed;
        opened = 1;
        /* LIFO: resume goes below the recipe, commands are pushed last-first. */
        if (sub_append(resume_cmd) < 0) goto failed;
        for (k = t->cmd_count - 1; k >= 0; k--) {
            if (is_make_verbose()) printf("INF: Batching %s\n", t->cmds[k]);
            if (sub_append(t->cmds[k]) < 0) goto failed;
        }
        if (sub_close() < 0) goto failed;
        return 0;
    }
    if (state_delete(STATE_DEFAULT_FILE) < 0) return 1;
    fprintf(stdout, "INF: Build complete.\n");
    return 0;

failed:
    fprintf(stderr, "ERR: Cannot commit build batch\n");
    if (opened && sub_abort() < 0) {
        fprintf(stderr, "ERR: Submit rollback failed; inspect $$$.SUB and make.sta\n");
        return 1;
    }
    if (make_abort_target(state) == 0) state_delete(STATE_DEFAULT_FILE);
    return 1;
}
