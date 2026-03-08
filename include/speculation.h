#ifndef __SPECULATION

#define FREE (0)
#define BUSY (1)

typedef struct _object_status {
    double current_time;
    int owner;
    double causality_violation_time;
    int standing_rollback;
    int in_stack;
    int the_state; // this is required in order to handle the rollback phase
                   // the two possible values are FREE (currently the object is not under control
                   // of any thread) and BUSY (it is under control by some thread)
    int already_taken;
#ifdef AVOID_THROTTLING
    int checkpointed;
#endif
} object_status;

int speculation_init(void);
void object_lock(int);
void object_unlock(int);
void put_into_stack(int);
void put_head_into_stack(int);
int get_from_stack(int *);
int run_rollback(int, double);

#endif
