#include <pthread.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "engine.h"
#include "queue.h"
#include "random.h"
#include "setup.h"

#ifdef SPECULATION
#include "speculation.h"
#endif
#if GRID_CKPT
#include "grid_ckpt.h"
#elif CHUNK_BASED
#include "chunk_ckpt.h"
#elif MMAP_MV
#include "mmap_mv.h"
#endif

#ifdef SPECULATION
void speculation_queue_flush(void);
void retractable_queue_flush(void);
static void retractable_queue_insert_locked(queue_elem *, int);
#ifdef DEBUG
void verify_empty_queues(void);
void verify_empty_speculation_queues(void);
void verify_empty_retractable_queues(void);
void verify_speculation_status(void);
void verify_queue_order(int);
void verify_retractable_queue_order(int);
#endif
#endif

slot queue[OBJECTS][NUM_SLOTS];
alignas(64) lock_buffer locks[OBJECTS][NUM_SLOTS];
// lock_buffer locks[OBJECTS][NUM_SLOTS];

#ifdef SPECULATION
slot retractable_queue[OBJECTS];
send_log log_queue[OBJECTS]; // this queue will just record <send_time,buffer_address> entries
#endif

double volatile current_min_limit = 0.0;
double volatile current_max_limit = NUM_SLOTS * LOOKAHEAD;
int volatile current_index = 0;

long pending_events __attribute__((aligned(64))) = 0;
long object_identifiers __attribute__((aligned(64))) = 0;
#ifdef SPECULATION
long shadow_object_identifiers __attribute__((aligned(64))) = 0;
long retractable_object_identifiers __attribute__((aligned(64))) = 0;
#endif
long object_identifiers_vector[MAX_NUMA_NODES] __attribute__((aligned(64))) = {[0 ... MAX_NUMA_NODES - 1] = 0};
int end = 0;

#ifdef SPECULATION
long speculation_events __attribute__((aligned(64))) = 0;
long retractable_events __attribute__((aligned(64))) = 0;
double filter_message[OBJECTS] = {(0.0 - epsilon)}; // this just initializes the 0-th entry to show
                                                    // the value that should be written across all
extern object_status speculation[OBJECTS];
uint64_t rollbacks __attribute__((aligned(64))) = 0;
#endif
uint64_t epochs = 0;

__thread int seen_empty_slot = 0;
__thread int my_index = 0;
__thread fallback_slot fallback_queue; // WE INITIALIZE VIA EMPTY ZERO MEMORY = { .head = NULL , .tail = NULL };

#ifdef SPECULATION
fallback_slot speculation_queue[OBJECTS] = {
    0}; // WE INITIALIZE VIA EMPTY ZERO MEMORY = { .head = NULL , .tail = NULL };
#endif

__thread unsigned me;
__thread unsigned target = -1;

// we use '_' here just to discriminate from the
// corresponding  non TLS global variables
__thread int *_c;
__thread int *_min;
__thread int *_max;
// these are used for NUMA aware workload distribution
__thread int myNUMAnode;
__thread int myNUMAindex;
__thread int stealNUMAindex;
__thread int TOT_NUMA_NODES;

void whoami(unsigned my_id) {
    AUDIT printf("just audit whoami: %u\n", my_id);
    me = my_id;
    _c = getcounter();
    _min = getmin();
    _max = getmax();
    myNUMAindex = myNUMAnode = get_NUMAnode();
    stealNUMAindex = 0;
    TOT_NUMA_NODES = get_totNUMAnodes();
}

void queue_init(void) {
    int i;
    int j;
    queue_elem *head;
    queue_elem *tail;

    for (j = 0; j < OBJECTS; j++) {
#ifdef SPECULATION
        head = &retractable_queue[j].head;
        tail = &retractable_queue[j].tail;
        head->next = tail; // setup initial double linked list
        head->prev = NULL;
        tail->next = NULL;
        tail->prev = head;
        head->timestamp = -1; // setup initial timestamp value
        tail->timestamp = -1;
#endif
        for (i = 0; i < NUM_SLOTS; i++) {
            head = &queue[j][i].head;
            tail = &queue[j][i].tail;
            head->next = tail; // setup initial double linked list
            head->prev = NULL;
            tail->next = NULL;
            tail->prev = head;
            head->timestamp = -1; // setup initial timestamp value
            tail->timestamp = -1;
            if (pthread_spin_init(&locks[j][i].lock, PTHREAD_PROCESS_PRIVATE)) {
                printf("Object %d - Failing pthread_spin_init for slot %d\n", j, i);
                exit(EXIT_FAILURE);
            }
        }
    }
}

void update_timing(void) {
    current_min_limit += LOOKAHEAD;
    current_max_limit += LOOKAHEAD;
    current_index = (current_index + 1) % NUM_SLOTS;
    epochs += 1;
    int i;

    AUDIT {
        printf("updating timing - current min is %e - current max is %e - current index is %d\n", current_min_limit,
               current_max_limit, current_index);
        fflush(stdout);
    }

    object_identifiers = 0;

#ifdef SPECULATION
    shadow_object_identifiers = 0;
    retractable_object_identifiers = 0;
#endif

    for (i = 0; i < MAX_NUMA_NODES; i++) {
        object_identifiers_vector[i] = 0;
    }

#ifdef SPECULATION
    if (!pending_events && !speculation_events) {
#else
    if (!pending_events) {
#endif
        end = 1;
    }
}

// Enqueues an event in the per-object queue for processing in the calculated epoch slot.
int queue_insert_in_epoch(queue_elem *elem) {
    queue_elem *current;
    queue_elem *tail;
    int destination;
    int index;

    destination = elem->destination;
    index = (int)((elem->timestamp) / (double)SLOT_LEN);
    index = index % NUM_SLOTS;

    AUDIT {
        printf("object %d - inserting event with timestamp %e in slot %d\n", destination, elem->timestamp, index);
        fflush(stdout);
    }

    pthread_spin_lock(&locks[destination][index].lock);

    current = queue[destination][index].head.next;
    tail = &queue[destination][index].tail;

    while (current != tail && current->timestamp <= elem->timestamp) {
        current = current->next;
    }
    elem->next = current;
    elem->prev = current->prev;
    current->prev->next = elem;
    current->prev = elem;
#ifdef DEBUG
    verify_queue_order(destination);
#endif
    pthread_spin_unlock(&locks[destination][index].lock);

    __sync_fetch_and_add(&pending_events, 1); // there is one more element in the queue
}

int queue_insert_from_fallback(queue_elem *elem) {
    queue_elem *current;
    queue_elem *tail;
    int destination;
    int index;

    destination = elem->destination;
    index = (int)((elem->timestamp) / (double)SLOT_LEN);
    index = index % NUM_SLOTS;

    AUDIT {
        printf("object %d - inserting event with timestamp %e in slot %d\n", destination, elem->timestamp, index);
        fflush(stdout);
    }

    pthread_spin_lock(&locks[destination][index].lock);

    current = queue[destination][index].head.next;
    tail = &queue[destination][index].tail;

    while (current != tail && current->timestamp <= elem->timestamp) {
        current = current->next;
    }
    elem->next = current;
    elem->prev = current->prev;
    current->prev->next = elem;
    current->prev = elem;
#ifdef DEBUG
    verify_queue_order(destination);
#endif
    pthread_spin_unlock(&locks[destination][index].lock);
}

int queue_insert_in_slot(queue_elem *elem, int index) {
    queue_elem *current;
    queue_elem *tail;
    int destination;

    destination = elem->destination;

    AUDIT {
        printf("object %d - inserting event with timestamp %e in slot %d\n", destination, elem->timestamp, index);
        fflush(stdout);
    }

    pthread_spin_lock(&locks[destination][index].lock);

    current = queue[destination][index].head.next;
    tail = &queue[destination][index].tail;

    while (current != tail && current->timestamp <= elem->timestamp) {
        current = current->next;
    }
    elem->next = current;
    elem->prev = current->prev;
    current->prev->next = elem;
    current->prev = elem;
#ifdef DEBUG
    verify_queue_order(destination);
#endif
    pthread_spin_unlock(&locks[destination][index].lock);

    __sync_fetch_and_add(&pending_events, 1);
}

int queue_insert(queue_elem *elem) {
    queue_elem *current;
    queue_elem *tail;
    int index;
    int destination = elem->destination;

    AUDIT printf("just audit who I am: %u\n", me);

    if (elem->timestamp < current_min_limit) {
        printf("object %d - illegal queue insert - timestamp is %e - min limit is %e\n", destination, elem->timestamp,
               current_min_limit);
        return -1;
    }

    if (elem->timestamp >= current_max_limit) {
        // here we make a tail insert
        elem->next = NULL;
        if (fallback_queue.head == NULL) {
            elem->prev = NULL;
            fallback_queue.head = elem;
            fallback_queue.tail = elem;
        } else {
            elem->prev = fallback_queue.tail;
            fallback_queue.tail->next = elem;
            fallback_queue.tail = elem;
        }
        __sync_fetch_and_add(&pending_events, 1);
        return 0;
    }

    index = (int)((elem->timestamp) / (double)SLOT_LEN);
    index = index % NUM_SLOTS;

    AUDIT {
        printf("object %d - inserting event with timestamp %e in slot %d\n", destination, elem->timestamp, index);
        fflush(stdout);
    }

    pthread_spin_lock(&locks[destination][index].lock);

    current = queue[destination][index].head.next;
    tail = &queue[destination][index].tail;

    while (current != tail && current->timestamp <= elem->timestamp) {
        current = current->next;
    }
    elem->next = current;
    elem->prev = current->prev;
    current->prev->next = elem;
    current->prev = elem;
#ifdef DEBUG
    verify_queue_order(destination);
#endif
    pthread_spin_unlock(&locks[destination][index].lock);

    __sync_fetch_and_add(&pending_events, 1); // there is one more element in the queue

    return 0;
}

void fallback_check(void) {
    queue_elem *current =
        fallback_queue.head; // fallback_queue is __thread, so access to this queue is already isolated.
    queue_elem *next;

    while (current) {
        next = current->next;
        if (current->timestamp < current_max_limit) {
            if (current->prev) {
                current->prev->next = current->next;
            } else {
                fallback_queue.head = current->next;
            }
            if (current->next) {
                current->next->prev = current->prev;
            } else {
                fallback_queue.tail = current->prev;
            }

            AUDIT {
                printf("object %d - queue_insert (from fallback) for an element with timestamp %e\n",
                       current->destination, current->timestamp);
                fflush(stdout);
            }

            queue_insert_from_fallback(current);
        }
        current = next;
    }
}

queue_elem *queue_extract() {
    int index;
    queue_elem *head;
    queue_elem *tail;
    queue_elem *elem;
#ifdef SPECULATION
    double rollback_time;
    if (target == -1) {
        get_from_stack(&target);
        AUDIT {
            printf("thread %d - get_from_stack: %d\n", me, target);
            fflush(stdout);
        }
    }
#endif

    AUDIT {
        printf("thread %d - extraction with target %d\n", me, target);
        fflush(stdout);
    }

start:
#ifndef NUMA_BALANCING
    if (target == -1) {
        target = __sync_fetch_and_add(&object_identifiers, 1);
    }
#else
    if (target == -1) {
    retry:
        target = __sync_fetch_and_add(&object_identifiers_vector[myNUMAindex], 1);
        if (target >= _c[myNUMAindex]) {
            if (stealNUMAindex < TOT_NUMA_NODES) {
                stealNUMAindex++;
                myNUMAindex = (myNUMAindex + 1) % TOT_NUMA_NODES;
                goto retry;
            } else {
                target = OBJECTS;
            }
        } else {
            target += _min[myNUMAindex];
        }
    }
#endif

redo:
    if (end) {
        return NULL;
    }
#ifdef DEBUG
    if (target == -1) {
        printf("ERROR: target management corrupted\n");
        exit(EXIT_FAILURE);
    }
#endif
    if (target < OBJECTS) {
#ifdef SPECULATION
        object_lock(target);
        if (speculation[target].the_state == FREE) {
            speculation[target].the_state = BUSY;
            speculation[target].owner = me;
        } else {
            if (speculation[target].owner != me) {
                object_unlock(target);
                target = -1;
                goto start;
            }
        }
#ifdef AVOID_THROTTLING
        if (speculation[target].checkpointed == 0) {
            set_ckpt(target);
            speculation[target].checkpointed = 1;
        }
#endif
        speculation[target].already_taken = 1;
        rollback_time = 0.0;
        if (speculation[target].standing_rollback) {
            rollback_time = speculation[target].causality_violation_time;
            speculation[target].standing_rollback = 0;
            speculation[target].current_time = current_min_limit;
            filter_message[target] = rollback_time - epsilon;
        }
        object_unlock(target);
        if (rollback_time > 0.0) {
            AUDIT {
                printf("thread %d - executing rollback for object %d\n", me, target);
                fflush(stdout);
            }
            run_rollback(target, rollback_time); // we restore the current epoch initial state of the object
                                                 // after we need to run this object as a normal execution
                                                 // but we need to avoid new events production up to the rollback_time
                                                 // that has been flushed to the filter_message[] entry of the object
#ifdef BENCHMARKING
            __sync_fetch_and_add(&rollbacks, 1);
#endif
            AUDIT {
                printf("thread %d - completed rollback for object %d\n", me, target);
                fflush(stdout);
            }
        }
#endif
#ifdef SPECULATION
        object_lock(target);
#endif
        index = my_index;
        head = &queue[target][index].head;
        tail = &queue[target][index].tail;

        if (head->next == tail) { // the current slot is empty - try with another target
#ifdef SPECULATION
            speculation[target].the_state = FREE;
            speculation[target].owner = -1;
            object_unlock(target);
            if (get_from_stack(&target)) {
                AUDIT {
                    printf("thread %d - get_from_stack: %d\n", me, target);
                    fflush(stdout);
                }
                goto redo;
            }
#endif
#ifndef NUMA_BALANCING
            target = __sync_fetch_and_add(&object_identifiers, 1);
#else
            target = __sync_fetch_and_add(&object_identifiers_vector[myNUMAindex], 1);
            if (target >= _c[myNUMAindex]) {
                goto retry;
            } else {
                target += _min[myNUMAindex];
            }
#endif
            goto redo;
        }
#ifdef SPECULATION
        object_unlock(target);
#endif
    } else {
#ifdef SPECULATION
        if (get_from_stack(&target)) {
            AUDIT {
                printf("thread %d - get_from_stack: %d\n", me, target);
                fflush(stdout);
            }
            goto redo;
        }
#endif
        AUDIT {
            printf("found empty slot with index %d\n", index);
            fflush(stdout);
        }

#ifdef SPECULATION
#ifdef DEBUG
        if (barrier()) {
            verify_empty_queues();
            verify_speculation_status();
        }
#endif
        barrier();
        retractable_queue_flush();
#ifdef DEBUG
        if (barrier()) {
            verify_empty_retractable_queues();
            /* if (retractable_events != 0) {
                printf("ERROR: retractable_events not zero (%d)!\n", retractable_events);
                exit(EXIT_FAILURE);
            } */
        }
#endif
        speculation_queue_flush();
#ifdef DEBUG
        if (barrier()) {
            verify_empty_speculation_queues();
            if (speculation_events != 0) {
                printf("ERROR: speculation_events not zero (%d)!\n", speculation_events);
                exit(EXIT_FAILURE);
            }
        }
#endif
#endif
        if (barrier()) {
            update_timing(); // this call updates the queue layout and releases the objects taken by threads in the last
                             // epoch
        }
        barrier();

        my_index = current_index;
        target = -1;
        // reset stuff for NUMA aware workload distribution
        myNUMAindex = myNUMAnode;
        stealNUMAindex = 0;
        fallback_check();
        barrier();
        goto start;
    }

#ifdef SPECULATION
    object_lock(target);
    if (speculation[target].standing_rollback == 1) {
        object_unlock(target);
        goto redo;
    }
#endif
    pthread_spin_lock(&locks[target][index].lock);

    if (head->next == tail) { // the current slot is empty
        pthread_spin_unlock(&locks[target][index].lock);
#ifdef SPECULATION
        if (speculation[target].in_stack) {
            printf("ERROR: stack management violation\n");
            fflush(stdout);
            exit(EXIT_FAILURE);
        }
        speculation[target].the_state = FREE;
        speculation[target].owner = -1;
        object_unlock(target);
        if (get_from_stack(&target)) {
            AUDIT {
                printf("thread %d - get_from_stack: %d\n", me, target);
                fflush(stdout);
            }
            goto redo;
        }
#endif
        if (end) {
            return NULL;
        } else {
            goto redo;
        }
    }

    // regular head extraction from a non-empty slot

    elem = head->next;

    head->next = elem->next;
    elem->next->prev = head;

    elem->next = NULL;
    elem->prev = NULL;
#ifdef DEBUG
    verify_queue_order(target);
#endif
    pthread_spin_unlock(&locks[target][index].lock);

#ifdef SPECULATION
#ifdef DEBUG
    if (speculation[target].current_time > elem->timestamp) {
        printf("ERROR: object %d - thread %d - causality violation order: current_time %e - event_time %e\n", target,
               me, speculation[target].current_time, elem->timestamp);
        printf("object %d - owner: %d - in_stack: %d - state: %d - standing_rollback: %d\n", target,
               speculation[target].owner, speculation[target].in_stack, speculation[target].the_state,
               speculation[target].standing_rollback);
        print_queues_status(target);
        exit(EXIT_FAILURE);
    }
#endif
    speculation[target].current_time = elem->timestamp;
    retractable_queue_insert_locked(elem, target);
    object_unlock(target);
    __sync_fetch_and_add(&retractable_events, 1);
    __sync_fetch_and_add(&pending_events, 1);
#endif

#ifndef SPECULATION
    __sync_fetch_and_add(&pending_events, -1);
#endif

    return elem;
}

#ifdef SPECULATION

static void retractable_queue_insert_locked(queue_elem *elem, int dest) {
    elem->prev = retractable_queue[dest].tail.prev;
    elem->next = &retractable_queue[dest].tail;
    retractable_queue[dest].tail.prev->next = elem;
    retractable_queue[dest].tail.prev = elem;
}

void log_the_send(queue_elem *the_elem, int current_object, double current_time) {
    send_log *queue = &log_queue[current_object];

    log_element *node = malloc(sizeof(log_element));
    if (!node) {
        printf("object %d - unable to allocate log entry\n", current_object);
        exit(EXIT_FAILURE);
    }

#ifdef DEBUG
    if (queue->tail && queue->tail->send_time > current_time) {
        printf("ERROR: object %d current_time is %e but last send is %e (destination is %d with timestamp %e)\n",
               current_object, current_time, queue->tail->send_time, the_elem->destination, the_elem->timestamp);
        exit(EXIT_FAILURE);
    }
#endif

    node->the_element = the_elem;
    node->send_time = current_time;

    node->next = NULL;
    node->prev = queue->tail;

    if (queue->tail) {
        queue->tail->next = node; /* append */
    } else {
        queue->head = node; /* first element */
    }

    queue->tail = node;
}

void flush_log(int object) {
    send_log *queue = &log_queue[object];
    log_element *current = queue->head;
    log_element *next;

    while (current) {
        next = current->next;
        free(current);
        current = next;
    }

    queue->head = NULL;
    queue->tail = NULL;
}

int speculation_queue_insert(queue_elem *elem) {
    int source = get_current();
    int destination = elem->destination;
    elem->send_time = get_current_time();

    AUDIT printf("just audit who I am: %u\n", me);

    // with speculaton we have a LOOKAHEAD wide time window for straggler acceptance
    if (elem->timestamp < current_min_limit) {
        printf("object %d - illegal speculation queue insert - timestamp is %e - min speculation limit is %e\n",
               destination, elem->timestamp, current_min_limit - LOOKAHEAD);
        return -1;
    }

    // this insertion into the speculation queue will be flushed to the actual input queue of the destination object if
    // the source will not rollback before the event generation
    if (elem->timestamp >= current_min_limit + LOOKAHEAD) {
        AUDIT {
            printf("object %d - inserting in speculation queue an event with timestamp %e for object %d\n", source,
                   elem->timestamp, destination);
            fflush(stdout);
        }
        // here we make a tail insert - there will be no next
        elem->next = NULL;
        if (speculation_queue[source].head == NULL) {
            elem->prev = NULL;
            speculation_queue[source].head = elem;
            speculation_queue[source].tail = elem;
        } else {
            elem->prev = speculation_queue[source].tail;
            speculation_queue[source].tail->next = elem;
            speculation_queue[source].tail = elem;
        }
        __sync_fetch_and_add(&speculation_events, 1);
        return 0;
    }

    // insert into the current epoch - if a rollback occurs,
    // the element’s address is logged so the insertion can be undone.
    log_the_send(elem, source, get_current_time());

    object_lock(destination);
    if (speculation[destination].current_time >= elem->timestamp) {
        AUDIT {
            printf("object %d - rollback needed (current_time %e) with rollback_time %e\n", elem->destination,
                   speculation[destination].current_time, elem->timestamp);
            fflush(stdout);
        }
        if (speculation[destination].standing_rollback) {
            if (speculation[destination].causality_violation_time > elem->timestamp) {
                speculation[destination].causality_violation_time = elem->timestamp;
            }
        } else {
            speculation[destination].standing_rollback = 1;
            speculation[destination].causality_violation_time = elem->timestamp;
            if (speculation[destination].the_state == FREE) { // get the object for processing
                speculation[destination].the_state = BUSY;
                speculation[destination].owner = me;
                put_head_into_stack(source);
                put_into_stack(destination);
                target = -1;
            }
        }
        queue_insert_in_epoch(elem);
    } else {
        queue_insert_in_epoch(elem);
        AUDIT {
            printf("object %d - inserted event in the current epoch with timestamp %e\n", destination, elem->timestamp);
            fflush(stdout);
        }
        if (speculation[destination].the_state == FREE && speculation[destination].already_taken) {
            speculation[destination].the_state = BUSY;
            speculation[destination].owner = me;
            put_into_stack(destination);
        }
    }
    object_unlock(destination);

    return 0;
}

void speculation_queue_flush(void) {
    unsigned target_object;

    AUDIT {
        printf("flush of the speculation queue called\n");
        fflush(stdout);
    }

flush_another:
    target_object = __sync_fetch_and_add(&shadow_object_identifiers, 1);
    if (target_object < OBJECTS) {
        AUDIT printf("flushing speculation queue for object %d\n", target_object);
        speculation[target_object].causality_violation_time = 0.0;
        speculation[target_object].standing_rollback = 0;
        speculation[target_object].the_state = FREE;
        speculation[target_object].owner = -1;
        speculation[target_object].already_taken = 0;
        filter_message[target_object] = current_min_limit + LOOKAHEAD - epsilon;

        flush_log(target_object);

        queue_elem *current = speculation_queue[target_object].head;
        queue_elem *next;
        int count = 0;
        while (current) {
            next = current->next;
            current->next = NULL;
            current->prev = NULL;
            AUDIT {
                printf("object %d - inserting in the queue an event with timestamp %e for object %d from speculation "
                       "queue\n",
                       target_object, current->timestamp, current->destination);
                fflush(stdout);
            }
            queue_insert(current);
            count++;
            current = next;
        }
        __sync_fetch_and_add(&speculation_events, -count);
        speculation_queue[target_object].head = NULL;
        speculation_queue[target_object].tail = NULL;
    } else {
        return;
    }
    AUDIT {
        printf("object %d - set checkpoint at %e\n", target_object, current_min_limit + LOOKAHEAD);
        fflush(stdout);
    }
#ifndef AVOID_THROTTLING
    set_ckpt(target_object);
#endif
#ifdef AVOID_THROTTLING
    speculation[target_object].checkpointed = 0;
#endif
    goto flush_another;
}

void retractable_queue_flush(void) {
    unsigned target_object;

    AUDIT {
        printf("retractable_queue_flush called\n");
        fflush(stdout);
    }

flush_another:
    target_object = __sync_fetch_and_add(&retractable_object_identifiers, 1);
    if (target_object < OBJECTS) {
        queue_elem *curr = retractable_queue[target_object].head.next;
        queue_elem *tail = &retractable_queue[target_object].tail;
        queue_elem *next;
        int count = 0;
        while (curr != tail) {
            next = curr->next;
            free(container_of(curr, event, q));
            count++;
            curr = next;
        }
        __sync_fetch_and_add(&retractable_events, -count);
        retractable_queue[target_object].head.next = tail;
        tail->prev = &retractable_queue[target_object].head;

    } else {
        return;
    }
    goto flush_another;
}

// this function is used to put into the per-object retractable queue an event that has been processed in the current
// epoch
void retractable_queue_insert(queue_elem *elem) {
    int dest;

    dest = elem->destination;

    AUDIT printf("just audit who I am: %u\n", me);

    if (elem->timestamp <
        current_min_limit) { // with speculation we have a LOOKAHEAD wide time window for straggler acceptance
        printf("illegal retractable queue insert - object is %d - timestamp is %e - min speculation limit is %e\n",
               dest, elem->timestamp, current_min_limit);
        fflush(stdout);
        exit(EXIT_FAILURE);
    }

    if (elem->timestamp >= (current_min_limit + LOOKAHEAD)) {
        printf(
            "illegal retractable queue insert - timestamp is %e - it oversteps the epoch limit plus lookahead (%e)\n",
            elem->timestamp, current_min_limit + LOOKAHEAD);
        fflush(stdout);
        exit(EXIT_FAILURE);
    }

    AUDIT {
        printf("object %d - inserting in retractable queue an event with timestamp %e\n", elem->destination,
               elem->timestamp);
        fflush(stdout);
    }
    object_lock(dest);
    // here we make a tail insert
    retractable_queue_insert_locked(elem, dest);
#ifdef DEBUG
    verify_retractable_queue_order(dest);
#endif
    object_unlock(dest);

    __sync_fetch_and_add(&retractable_events, 1);
    __sync_fetch_and_add(&pending_events, 1);
}

void rollback_speculation_queue(int object, double rollback_time) {
    fallback_slot *q = &speculation_queue[object];
    queue_elem *current;
    queue_elem *prev;

    AUDIT {
        printf("object %d - rollback_speculation_queue called with rollback_time %e\n", object, rollback_time);
        fflush(stdout);
    }

    if (q->head == NULL) {
        return;
    }

    current = q->tail;

    /* walk backwards removing elements >= rollback_time */
    while (current && current->send_time >= rollback_time) {
        prev = current->prev;
        free(container_of(current, event, q));
        current = prev;
        __sync_fetch_and_add(&speculation_events, -1);
    }

    q->tail = current;

    if (current) {
        current->next = NULL;
    } else {
        q->head = NULL;
    }
}

void queue_elem_annihilation(int source, queue_elem *the_elem) {
    int object = the_elem->destination;
    double cancellation_time = the_elem->timestamp;
    AUDIT {
        printf("queue_elem_annihilation for object %d with cancellation_time %e caused by %d\n", object,
               cancellation_time, source);
        fflush(stdout);
    }
    object_lock(object);
    if (speculation[object].standing_rollback) {
        if (speculation[object].causality_violation_time > cancellation_time) {
            speculation[object].causality_violation_time = cancellation_time;
        }
    } else {
        speculation[object].standing_rollback = 1;
        speculation[object].causality_violation_time = cancellation_time;
        if (speculation[object].the_state == FREE) {
            speculation[object].the_state = BUSY;
            speculation[object].owner = me;
            put_into_stack(object);
            // put_head_into_stack(source);
            // target = -1;
        }
    }
    if (speculation[object].current_time >
        the_elem->timestamp) { // remove the event to be annihilated from the retractable_queue
        the_elem->prev->next = the_elem->next;
        the_elem->next->prev = the_elem->prev;
        __sync_fetch_and_add(&retractable_events, -1);
#ifdef DEBUG
        verify_retractable_queue_order(object);
#endif
    } else { // remove the event to be annihilated from the queue
        pthread_spin_lock(&locks[object][my_index].lock);
        the_elem->prev->next = the_elem->next;
        the_elem->next->prev = the_elem->prev;
#ifdef DEBUG
        verify_queue_order(object);
#endif
        pthread_spin_unlock(&locks[object][my_index].lock);
        __sync_fetch_and_add(&pending_events, -1);
    }
    free(container_of(the_elem, event, q));
    object_unlock(object);
}

void log_rollback(int object, double rollback_time) {
    send_log *queue = &log_queue[object];
    log_element *current;

    AUDIT {
        printf("object %d - log_rollback called with rollback_time %e\n", object, rollback_time);
        fflush(stdout);
    }

    current = queue->tail;

    /* Remove only from tail — monotonic log assumption */
    while (current && current->send_time >= rollback_time) {

        log_element *prev = current->prev;

        queue_elem_annihilation(object, current->the_element);
        free(current);

        current = prev;
    }

    queue->tail = current;

    if (current) {
        current->next = NULL;
    } else {
        queue->head = NULL; /* list became empty */
    }
}

void restore_retractable_events(int object) {
    queue_elem *head, *tail, *current;
    int index;
    int count = 0;

    object_lock(object);
    head = &retractable_queue[object].head;
    tail = &retractable_queue[object].tail;

    if (head->next != tail) {
        index = (int)((head->next->timestamp) / (double)SLOT_LEN);
        index = index % NUM_SLOTS;
    } else {
        object_unlock(object);
        return;
    }

    while (head->next != tail) {
        current = head->next;

        head->next = current->next;
        current->next->prev = head;

        current->next = NULL;
        current->prev = NULL;
        count++;
        queue_insert_in_slot(current, index);
#ifdef DEBUG
        verify_retractable_queue_order(object);
#endif
    }
    object_unlock(object);

    __sync_fetch_and_add(&retractable_events, -count);
}

void print_queues_status(int object) {
    queue_elem *current = queue[object][my_index].head.next;
    while (current != &queue[object][my_index].tail) {
        printf("object %d - event in queue with timestamp %e\n", object, current->timestamp);
        fflush(stdout);
        current = current->next;
    }
    current = retractable_queue[object].head.next;
    while (current != &retractable_queue[object].tail) {
        printf("object %d - event in retractable_queue with timestamp %e\n", object, current->timestamp);
        fflush(stdout);
        current = current->next;
    }
    current = speculation_queue[object].head;
    while (current != NULL) {
        printf("object %d - event in speculation_queue with timestamp %e\n", object, current->timestamp);
        fflush(stdout);
        current = current->next;
    }
}

#ifdef DEBUG
void verify_empty_queues(void) {
    for (int i = 0; i < OBJECTS; i++) {
        if (queue[i][my_index].head.next != &queue[i][my_index].tail) {
            print_queues_status(i);
            printf("object %d - owner: %d - in_stack: %d - state: %d - standing_rollback: %d - already_taken: %d\n", i,
                   speculation[i].owner, speculation[i].in_stack, speculation[i].the_state,
                   speculation[i].standing_rollback, speculation[i].already_taken);
            printf("target counter status: %d\n", object_identifiers_vector[myNUMAindex]);
            print_queues_status(i);
            exit(EXIT_FAILURE);
        }
    }
}

void verify_empty_speculation_queues() {
    for (int i = 0; i < OBJECTS; i++) {
        if (speculation_queue[i].head != NULL || speculation_queue[i].tail != NULL) {
            printf("object %d - in_stack: %d - state: %d - standing_rollback: %d\n", i, speculation[i].in_stack,
                   speculation[i].the_state, speculation[i].standing_rollback);
            printf("target counter status: %d\n", object_identifiers_vector[myNUMAindex]);
            print_queues_status(i);
            exit(EXIT_FAILURE);
        }
    }
}

void verify_empty_retractable_queues() {
    for (int i = 0; i < OBJECTS; i++) {
        if (retractable_queue[i].head.next != &retractable_queue[i].tail) {
            printf("object %d - in_stack: %d - state: %d - standing_rollback: %d\n", i, speculation[i].in_stack,
                   speculation[i].the_state, speculation[i].standing_rollback);
            printf("target counter status: %d\n", object_identifiers_vector[myNUMAindex]);
            print_queues_status(i);
            exit(EXIT_FAILURE);
        }
    }
}

void verify_speculation_status(void) {
    for (int i = 0; i < OBJECTS; i++) {
        if (speculation[i].standing_rollback == 1) {
            printf("ERROR: object %d has a standing rollback\n", i);
            printf("object %d - owner: %d - in_stack: %d - state: %d - standing_rollback: %d - already_taken: %d\n", i,
                   speculation[i].owner, speculation[i].in_stack, speculation[i].the_state,
                   speculation[i].standing_rollback, speculation[i].already_taken);
            printf("target counter status: %d\n", object_identifiers_vector[myNUMAindex]);
            print_queues_status(i);
            exit(EXIT_FAILURE);
        }
        if (speculation[i].owner != -1) {
            printf("ERROR: object %d has a owner\n", i);
            printf("object %d - owner: %d - in_stack: %d - state: %d - standing_rollback: %d - already_taken: %d\n", i,
                   speculation[i].owner, speculation[i].in_stack, speculation[i].the_state,
                   speculation[i].standing_rollback, speculation[i].already_taken);
            printf("target counter status: %d\n", object_identifiers_vector[myNUMAindex]);
            print_queues_status(i);
            exit(EXIT_FAILURE);
        }
    }
}

void verify_queue_order(int object) {
    queue_elem *current;
    queue_elem *tail;
    current = queue[object][my_index].head.next;
    tail = &queue[object][my_index].tail;
    if (current == tail || current->next == tail) {
        return;
    }

    while (current->next != tail) {
        if (current->timestamp > current->next->timestamp) {
            printf("ERROR: object %d wrong queue order\n", object);
            exit(EXIT_FAILURE);
        }
        current = current->next;
    }
}

void verify_retractable_queue_order(int object) {
    queue_elem *current;
    queue_elem *tail;
    current = retractable_queue[object].head.next;
    tail = &retractable_queue[object].tail;
    if (current == tail || current->next == tail) {
        return;
    }

    while (current->next != tail) {
        if (current->timestamp > current->next->timestamp) {
            printf("ERROR: object %d wrong retractable_queue order\n", object);
            exit(EXIT_FAILURE);
        }
        current = current->next;
    }
}

#endif

#endif
