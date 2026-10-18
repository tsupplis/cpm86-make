/* Host-side fault injection around real state, recipe and execution code.
 * Run only in a disposable directory (tests/regression.py does this). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef MAKE_HOST_TEST
#include <unistd.h>
#include <sys/resource.h>
#include <signal.h>
#else
extern int unlink();
#endif
#include "make.h"

static make_graph_t graph;
static make_state_t state;
static int fail_save, fail_open, fail_append, fail_close, fail_stamp;
static int pending, base, appended, aborted, launched, attached, launch_error;
static int create_output, fail_priority, fail_attach, positive_stamps;
static long last_stamp;
static int parent_priority, priority_calls, priority_values[4], event_count;
static char events[8];
static char pushed[17][126];

int real_state_save(make_state_t *, char *);
int state_save(make_state_t *s, char *path)
{
    return fail_save ? -1 : real_state_save(s, path);
}
int make_file_exists(char *name)
{
    FILE *fp = fopen(name, "r");
    if (!fp) return 0;
    fclose(fp);
    return 1;
}
make_target_t *make_find_target(make_graph_t *g, char *name)
{
    int i;
    for (i = 0; i < g->target_count; i++)
        if (!strcmp(g->targets[i].name, name)) return &g->targets[i];
    return NULL;
}
int is_make_verbose(void) { return 0; }
long epoch_now(void) { return 1000L; }
int make_set_timestamp(char *db, char *name, long stamp)
{
    last_stamp = stamp;
    if (fail_stamp == 1 || (fail_stamp == 2 && stamp)) return -1;
    if (stamp) positive_stamps++;
    return 0;
}
int sub_records(void) { return pending; }
int sub_open(int flags) { base = pending; return fail_open ? -1 : 0; }
int sub_append(char *command)
{
    if (++appended == fail_append) return -1;
    strcpy(pushed[pending - base], command);
    pending++;
    return 0;
}
int sub_close(void) { return fail_close ? -1 : 0; }
int sub_abort(void) { pending = base; aborted++; return 0; }
int getppriority(void) { return parent_priority; }
int p_cli(char *command)
{
    FILE *fp;
    launched++;
    events[event_count++] = 'L';
    if (create_output) {
        fp = fopen("result.o", "w");
        fputs("NEW", fp);
        fclose(fp);
    }
    return launch_error;
}
int p_priority(int priority)
{
    priority_values[priority_calls++] = priority;
    events[event_count++] = 'P';
    return fail_priority ? -1 : 0;
}
int conattach(void)
{
    attached++;
    events[event_count++] = 'A';
    return fail_attach ? -1 : 0;
}

static void require(int condition, char *what)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", what); exit(1); }
}
static void output(char *text)
{
    FILE *fp = fopen("result.o", "w");
    require(fp != NULL, "create fixture");
    fputs(text, fp);
    fclose(fp);
}
static int old_output(void)
{
    FILE *fp = fopen("result.o", "r");
    char text[4];
    memset(text, 0, sizeof(text));
    if (!fp) return 0;
    fread(text, 1, 3, fp);
    fclose(fp);
    return !strcmp(text, "OLD");
}
static void setup(void)
{
    unlink("make.sta"); unlink("make.bak"); unlink("result.o");
    memset(&graph, 0, sizeof(graph));
    graph.target_count = 1;
    strcpy(graph.targets[0].name, "result.o");
    graph.targets[0].cmd_count = 2;
    graph.targets[0].cmds[0] = "compile result.o";
    graph.targets[0].cmds[1] = "finish result.o";
    state_init(&state, "build.mk", "testts", "result.o");
    state_push(&state, "result.o");
    fail_save = fail_open = fail_append = fail_close = fail_stamp = 0;
    pending = 4; base = appended = aborted = launched = attached = 0;
    launch_error = create_output = fail_priority = fail_attach = positive_stamps = 0;
    last_stamp = -1;
    parent_priority = 200;
    priority_calls = event_count = 0;
    output("OLD");
}
static int submit(void)
{
    return sub_process(&state, &graph, state.makefile, state.dbname, state.main_target);
}
static int execute(void)
{
    return exec_process(&state, &graph, state.makefile, state.dbname, state.main_target);
}
int main(void)
{
    make_state_t loaded;
    FILE *fp;
    int i;
#ifdef MAKE_HOST_TEST
    struct rlimit original_limit, short_limit;
#endif
    setup();
    require(submit() == 0, "submit succeeds");
    require(pending == 7 && !make_file_exists("result.o") && make_file_exists("make.bak"), "output preserved before submit");
    require(!strcmp(pushed[1], "finish result.o") && !strcmp(pushed[2], "compile result.o"), "submit LIFO order");
    require(state_load(&loaded, "make.sta") == 0 && !strcmp(loaded.backup, "make.bak"), "state roundtrip");
    require(make_finish_target(&state, &graph.targets[0]) < 0, "missing new output fails");
    require(make_abort_target(&state) == 0 && old_output(), "missing output restores original");

    for (i = 0; i < 7; i++) {
        setup();
        if (i == 0) fail_save = 1;
        if (i == 1) fail_open = 1;
        if (i == 2) fail_append = 1;
        if (i == 3) fail_append = 2;
        if (i == 4) fail_append = 3;
        if (i == 5) fail_close = 1;
        if (i == 6) pending = 127;
        require(submit() != 0, "submit faults propagate");
        require(pending == (i == 6 ? 127 : 4), "caller submit preserved");
        require(old_output() && !make_file_exists("make.bak"), "submit fault preserves output");
        require(!make_file_exists("make.sta") && positive_stamps == 0, "submit fault not committed");
        if (i >= 2 && i <= 5) require(aborted == 1, "partial append rolled back");
    }
    setup(); launch_error = 255;
    require(execute() != 0 && launched == 1 && attached == 0, "launch failure stops before wait/next command");
    require(old_output() && !make_file_exists("make.sta") && positive_stamps == 0, "launch failure restores output");
    setup();
    require(execute() != 0 && old_output() && positive_stamps == 0, "successful launch without output fails");
    setup(); create_output = 1;
    require(execute() == 0 && !old_output() && positive_stamps == 1 && !make_file_exists("make.bak"), "new output committed");
    setup(); fail_stamp = 1;
    require(execute() != 0 && launched == 0 && old_output(), "timestamp invalidation failure stops launch");
    setup(); fail_stamp = 2; create_output = 1;
    require(execute() != 0 && old_output() && last_stamp == 0, "failed positive stamp restores old output and invalidates metadata");
    setup(); fail_priority = fail_attach = create_output = 1; parent_priority = 100;
    require(execute() == 0 && launched == 2 && attached == 2, "priority and attach return values do not stop commands");
    require(priority_calls == 4 && priority_values[0] == 99 && priority_values[1] == 100 &&
            priority_values[2] == 99 && priority_values[3] == 100,
            "wait priority is below parent and restores to 100");
    require(event_count == 8 && !memcmp(events, "LPAPLPAP", 8), "each launch performs setprio attach setprio in order");
    setup(); fail_save = 1;
    require(execute() != 0 && launched == 0 && old_output(), "state failure prevents launch");
    setup();
    require(make_prepare_target(&state, &graph.targets[0]) == 0, "prepare reset fixture");
    output("PARTIAL");
    require(make_abort_target(&state) == 0 && old_output(), "reset restores old output over partial output");
    setup();
#ifdef MAKE_HOST_TEST
    require(getrlimit(RLIMIT_FSIZE, &original_limit) == 0, "read file size limit");
    short_limit = original_limit;
    short_limit.rlim_cur = 1;
    signal(SIGXFSZ, SIG_IGN);
    require(setrlimit(RLIMIT_FSIZE, &short_limit) == 0, "inject short state write");
    require(real_state_save(&state, "make.sta") < 0, "buffered state write errors detected");
    require(setrlimit(RLIMIT_FSIZE, &original_limit) == 0, "restore file size limit");
#endif
    fp = fopen("make.sta", "w");
    fputs("makefile=build.mk\ncount=0\n", fp); fclose(fp);
    require(state_load(&loaded, "make.sta") < 0, "legacy/truncated state rejected");
    setup(); real_state_save(&state, "make.sta");
    fp = fopen("make.sta", "a"); fputs("target=unexpected\n", fp); fclose(fp);
    require(state_load(&loaded, "make.sta") < 0, "records after footer rejected");
    puts("PASS: execution, submit rollback, backup recovery and state I/O faults");
    return 0;
}
