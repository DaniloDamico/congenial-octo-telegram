#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "engine.h"
#include "queue.h"

#ifdef SPECULATION
#include "speculation.h"
extern double filter_message[OBJECTS];
#ifdef BENCHMARKING
uint64_t filtered_events __attribute__((aligned(64))) = 0;
#endif
#endif

int ScheduleNewEvent(int destination, double timestamp, int event_type, char *body, int size) {

    event *p = NULL;

#ifdef SPECULATION
    int flag;
    int source;
    source = get_current();
#endif

    AUDIT {
        printf("entered in ScheduleNewEvent with %d destination and %e timestamp\n", destination, timestamp);
        fflush(stdout);
    }

    if (size > MAX_EVENT_SIZE) {
        printf("event size too large\n");
        fflush(stdout);
        return -1;
    }

    if (size < 0) {
        printf("negative event size\n");
        fflush(stdout);
        return -1;
    }

    if ((size > 0) && (body == NULL)) {
        printf("invalid null address for the event body\n");
        fflush(stdout);
        return -1;
    }

#ifdef SPECULATION
    object_lock(source);
    flag = 0;
    AUDIT {
        printf("object %d - filter is %e - current time is %e\n", source, filter_message[source], get_current_time());
        fflush(stdout);
    }
    if (filter_message[source] >= get_current_time()) { // this is classical coasting forward event
        flag = 1;
    }
    object_unlock(source);
    if (flag) {
#ifdef BENCHMARKING
        __sync_fetch_and_add(&filtered_events, 1);
#endif
        AUDIT {
            printf("object %d - message filtered", source);
            fflush(stdout);
        }
        return 0;
    }
#endif

    p = malloc(sizeof(event)); // allocating the actual storage for the event
    if (!p) {
        printf("event allocation failure\n");
        fflush(stdout);
        return -1;
    }

    p->e.destination = destination;
    p->e.timestamp = timestamp;
    p->e.event_type = event_type;
    p->e.event_size = size;
    memcpy(&(p->e.payload), body, size);

    p->q.destination = destination;
    p->q.timestamp = timestamp;

#ifdef SPECULATION
    if (get_current_time() == STARTUP_TIME) {
        AUDIT {
            printf("object %d - inserting an event at startup as committed\n", p->e.destination);
            fflush(stdout);
        }
        return queue_insert(&(p->q)); // we can only commit simulation init events
    }
    AUDIT printf("schedule event called after startup\n");
    return speculation_queue_insert(&(p->q)); // all the others must pass through the speculation queue
#else
    return queue_insert(&(p->q));
#endif
}

int GetEvent(int *destination, double *timestamp, int *event_type, char *body, int *size) {
    event_buffer *e;
    queue_elem *q;

    q = queue_extract();
    if (q == NULL) {
        printf("empty event queue\n");
        return -1;
    }
    e = (event_buffer *)container_of(q, event, q);

    *destination = e->destination;
    *timestamp = e->timestamp;
    *event_type = e->event_type;
    *size = e->event_size;

    memcpy(body, e->payload, e->event_size);

#ifdef SPECULATION
    retractable_queue_insert(q);
#else
    free(e);
#endif

    return 0;
}
