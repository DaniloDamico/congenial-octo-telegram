
#include <pthread.h>
#include <queue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>

long control_counter __attribute__((aligned(64))) = THREADS;
long era_counter __attribute__((aligned(64))) = THREADS;

int barrier(void) {

    int ret;

    while (era_counter != THREADS && control_counter == THREADS)
        ;

    ret = __sync_bool_compare_and_swap(&control_counter, THREADS, 0);
    if (ret) {
        era_counter = 0;
    }

    __sync_fetch_and_add(&control_counter, 1);
    while (control_counter != THREADS)
        ;
    __sync_fetch_and_add(&era_counter, 1);

    return ret;
}
