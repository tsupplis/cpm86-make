#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include "make.h"
#include "util.h"

char sys_buf[128+1];
static int sys(int prio, char *command) {
    int c;

    strcpy(sys_buf, command);
    make_strupper(sys_buf);
    fprintf(stdout, "%s", sys_buf);
    c=p_cli(command);
    p_priority(197);
    conattach();
    p_priority(200);
    fprintf(stdout, "\n\n");
    return(c);
}

int exec_process(make_state_t *state, make_graph_t *graph, char *makefile, char *dbname, 
    char *target) {
    int k;
    char next_tgt[STATE_NAME_LEN];
    make_target_t *t;
    char exec_command[128];
    int pprio=getppriority();

    if(pprio <= 150) {
        fprintf(stderr, "ERR: Parent Priority too low (%d)\n", pprio);
        return -1;
    }
    while(!state_pop(state, next_tgt, sizeof(next_tgt))) {
        t = make_find_target(graph, next_tgt);
        if (!t || t->cmd_count == 0) {
            /* No commands for this target (e.g. meta/phony target).
               Only record a timestamp for a non-phony target that exists on disk.
            */
            if (!(t && t->is_phony)) {
                if (make_file_exists(next_tgt)) {
                    long now = epoch_now();
                    if (now == EPOCH_ERR)
                        now = 1L;
                    make_set_timestamp(dbname, next_tgt, now);
                } else {
                    fprintf(stderr, "ERR: Target '%s' not created\n", next_tgt);
                    state_delete(STATE_DEFAULT_FILE);
                    return 1;
                }
            }

            /* If there are more targets in stack, re-run make */
            if (state_count(state) > 0) {
                fprintf(stdout, "INF: Target '%s' complete.\n", next_tgt);
            } else {
                fprintf(stdout, "INF: Build complete.\n");
                return 0;
            }
        } else {
            for (k = 0; k < t->cmd_count; k++) {
                sys(pprio -1, t->cmds[k].text);
            }
            if (!(t && t->is_phony)) {
                if (make_file_exists(next_tgt)) {
                    long now = epoch_now();
                    if (now == EPOCH_ERR)
                        now = 1L;
                    make_set_timestamp(dbname, next_tgt, now);
                } else {
                    fprintf(stderr, "ERR: Target '%s' not created\n", next_tgt);
                    state_delete(STATE_DEFAULT_FILE);
                    return 1;
                }
            }
        }
    }
    fprintf(stdout, "INF: Build complete.\n");
    return 0;
}

