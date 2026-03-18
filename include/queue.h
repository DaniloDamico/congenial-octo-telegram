#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>

#include "setup.h"

#define NUM_SLOTS (2000)
#define SLOT_LEN (LOOKAHEAD)
#define QUEUE_TIME_INTERVAL (SLOT_LEN * NUM_SLOTS)
typedef struct _queue_elem {
    int destination;
    double timestamp;
#ifdef SPECULATION
    double send_time; // this is required to keep track of what to undo at sender side
                      // if some straggler hits an object
#endif
    struct _queue_elem *next;
    struct _queue_elem *prev;
} queue_elem;

typedef struct _slot {
    queue_elem head;
    queue_elem tail;
} slot;

typedef struct _fallbacks_slot {
    queue_elem *head;
    queue_elem *tail;
} fallback_slot;

typedef union _lock_buffer {
    pthread_spinlock_t lock;
    char buff[64];
} lock_buffer;

typedef struct _log_send {
    struct _log_element *head;
    struct _log_element *tail;
} send_log;

typedef struct _log_element {
    queue_elem *the_element;
    double send_time;
    struct _log_element *next;
    struct _log_element *prev;
} log_element;

void queue_init(void);
void whoami(unsigned);
int queue_insert(queue_elem *elem);
queue_elem *queue_extract(void);
int barrier(void);
#ifdef SPECULATION
int speculation_queue_insert(queue_elem *elem);
void retractable_queue_insert(queue_elem *elem);
void rollback_retractable_queue(int, double);
void rollback_speculation_queue(int, double);
void log_rollback(int, double);
void restore_retractable_events(int);
void print_queues_status(int);
#endif

#define offsetof(TYPE, MEMBER) ((size_t)&((TYPE *)0)->MEMBER)

#define container_of(ptr, type, member)                                                                                \
    ({                                                                                                                 \
        const typeof(((type *)0)->member) *__mptr = (ptr);                                                             \
        (type *)((char *)__mptr - offsetof(type, member));                                                             \
    })

#endif
