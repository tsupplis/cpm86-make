#include <stdio.h>
#include <ctype.h>
#include "make.h"
#include "util.h"

extern int bdos();
extern int getusr();

/* Echo the command like the CCP prompt does: <user><drive>>COMMAND */
static void echo_command(char *command)
{
    printf("%d%c>", getusr() & 0xff, 'A' + (bdos(25, 0) & 0x0f));
    while (*command) putchar(toupper(*command++ & 0xff));
    putchar('\n');
}

static int run_command(int parent_priority, char *command)
{
    int rc;
    echo_command(command);
    rc = p_cli(command);
    if (rc != 0) {
        fprintf(stderr, "ERR: Cannot launch '%s' (%d)\n", command, rc);
        return -1;
    }
    /* Run ahead of the parent while waiting to reclaim the console.
       These BDOS calls form a sequence; their return values are not status. */
    p_priority(parent_priority - 1);
    conattach();
    fprintf(stdout,"\n");
    p_priority(200);
    return 0;
}

int exec_process(make_state_t *state, make_graph_t *graph, char *makefile,
                 char *dbname, char *target)
{
    int k, rc;
    char next_tgt[STATE_NAME_LEN];
    make_target_t *t;
    int pprio = getppriority();

    if (pprio <= 0) {
        fprintf(stderr, "ERR: Invalid parent priority (%d)\n", pprio);
        return 1;
    }
    while (state_pop(state, next_tgt, sizeof(next_tgt)) == 0) {
        t = make_find_target(graph, next_tgt);
        if (!t) return 1;
        if (make_prepare_target(state, t) < 0) return 1;
        /* Also preserve recovery information for interrupted concurrent builds. */
        if (state_save(state, STATE_DEFAULT_FILE) < 0) goto failed;
        for (k = 0; k < t->cmd_count; k++) {
            rc = run_command(pprio, t->cmds[k]);
            if (rc < 0) goto failed;
        }
        if (make_finish_target(state, t) < 0) goto failed;
        if (state_save(state, STATE_DEFAULT_FILE) < 0) return 1;
    }
    if (state_delete(STATE_DEFAULT_FILE) < 0) return 1;
    fprintf(stdout, "INF: Build complete.\n");
    return 0;

failed:
    if (make_abort_target(state) == 0) state_delete(STATE_DEFAULT_FILE);
    return 1;
}
