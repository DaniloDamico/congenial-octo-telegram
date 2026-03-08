#include <pthread.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "queue.h"
#include "random.h"
#include "run.h"
#include "speculation.h"

#if GRID_CKPT
#include "grid_ckpt.h"
#elif CHUNK_BASED
#include "chunk_ckpt.h"
#endif

#define STACKABLE_OBJECTS (OBJECTS)

object_status speculation[OBJECTS]; // an entry of this array needs to be managed in separation
                                    // via the corresponding speculation_lock[] acquisition
alignas(64) lock_buffer speculation_locks[OBJECTS];

extern double filter_message[OBJECTS];
extern send_log log_queue[OBJECTS];

__thread int object_stack[STACKABLE_OBJECTS];
__thread int stack_index = -1;

extern __thread unsigned me;

int speculation_init(void) {
    int i;
    int j;

    for (j = 0; j < OBJECTS; j++) {
        pthread_spin_init(&(speculation_locks[j].lock), PTHREAD_PROCESS_PRIVATE);
        filter_message[j] = 0.0 - epsilon;
        log_queue[j].head = NULL;
        log_queue[j].tail = NULL;
        speculation[j].owner = -1;
#ifdef AVOID_THROTTLING
        speculation[j].checkpointed = 1;
#endif
    }
    return 1;
}

void object_lock(int object) {
    pthread_spin_lock(&speculation_locks[object].lock);
    return;
}

void object_unlock(int object) {
    pthread_spin_unlock(&speculation_locks[object].lock);
    return;
}

void put_into_stack(int object) {
    stack_index++;
    AUDIT {
        printf("thread %d - putting %d into the stack\n", me, object);
        fflush(stdout);
    }
    if (stack_index >= STACKABLE_OBJECTS) { // no more room in the stack
        printf("no more room into the object stack\n");
        fflush(stdout);
        exit(EXIT_FAILURE);
    }
    object_stack[stack_index] = object;
    speculation[object].in_stack = 1;
}

void put_head_into_stack(int object) {
    int i;
    stack_index++;
    AUDIT {
        printf("thread %d - putting %d head into the stack\n", me, object);
        fflush(stdout);
    }
    if (stack_index >= STACKABLE_OBJECTS) { // no more room in the stack
        printf("no more room into the object stack\n");
        fflush(stdout);
        exit(EXIT_FAILURE);
    }
    for (i = stack_index; i > 0; i--) {
        object_stack[i] = object_stack[i - 1]; // make room for a lower priority object
                                               // to be put in the 0-th entry
    }
    object_stack[0] = object; // finalize the insertion
    speculation[object].in_stack = 1;
}

int get_from_stack(int *object) {
    if (stack_index == -1) {
        return 0;
    }
    *object = object_stack[stack_index];
    stack_index--;
    speculation[*object].in_stack = 0;
    return 1;
}

void restore_state(int object) {
    AUDIT {
        printf("object %d - restore_state called\n", object);
        fflush(stdout);
    }
    restore_object(object); // rollback grid/chunk
}

int run_rollback(int object, double rollback_time) {
    AUDIT {
        printf("object %d - rollback with rollback_time %e\n", object, rollback_time);
        fflush(stdout);
    }
    restore_state(object); // here we default to the initial state of the current epoch
    // but remember to reset the object_status structure
    AUDIT {
        printf("object %d - restore_state completed\n", object);
        fflush(stdout);
    }
    rollback_speculation_queue(object, rollback_time);
    AUDIT {
        printf("object %d - rollback_speculation_queue completed\n", object);
        fflush(stdout);
    }
    log_rollback(object, rollback_time);
    AUDIT {
        printf("object %d - log_rollback completed\n", object);
        fflush(stdout);
    }
    restore_retractable_events(object); // we simply reput stuff in the input queue
    AUDIT {
        printf("object %d - restore_retractable_events completed\n", object);
        fflush(stdout);
    }
    return 0;
}
