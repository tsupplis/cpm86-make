/*
 * state.h -- CP/M-86 Make State Management on Disk
 *
 * Licensed under the MIT license. See LICENSE file in the project root.
 */
#ifndef STATE_H
#define STATE_H

#define STATE_MAX_TARGETS 64
#define STATE_NAME_LEN    64
#define STATE_DEFAULT_FILE "make.sta"

typedef struct _make_state_t {
    char makefile[STATE_NAME_LEN];
    char dbname[STATE_NAME_LEN];
    char main_target[STATE_NAME_LEN];
    char current_target[STATE_NAME_LEN];
    int  target_count;
    char targets[STATE_MAX_TARGETS][STATE_NAME_LEN];
} make_state_t;

#ifndef __STDC__
int   state_init();
int   state_push();
int   state_pop();
char *state_peek();
int   state_count();
void  state_set_current();
char *state_get_current();
int   state_save();
int   state_load();
int   state_exists();
int   state_delete();
#else
int   state_init(make_state_t *st, char *makefile, char *dbname, char *main_target);
int   state_push(make_state_t *st, char *target);
int   state_pop(make_state_t *st, char *target_buf, int buf_size);
char *state_peek(make_state_t *st);
int   state_count(make_state_t *st);
void  state_set_current(make_state_t *st, char *target);
char *state_get_current(make_state_t *st);
int   state_save(make_state_t *st, char *filepath);
int   state_load(make_state_t *st, char *filepath);
int   state_exists(char *filepath);
int   state_delete(char *filepath);
#endif

#endif
