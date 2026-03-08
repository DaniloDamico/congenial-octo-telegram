#ifndef CURRENT_H
#define CURRENT_H

#include "queue.h"

int get_current(void);
double get_current_time(void);

#define MAX_EVENT_SIZE (512)

typedef struct _event_buffer {
    int destination;
    double timestamp;
    double send_time;
    int event_type;
    char payload[MAX_EVENT_SIZE];
    int event_size;
} event_buffer;

typedef struct _event {
    event_buffer e;
    queue_elem q;
} event;


#endif

