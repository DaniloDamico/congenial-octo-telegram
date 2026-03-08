#ifndef SETUP_H
#define SETUP_H

#include <stdint.h>

#include "run.h"

#define MEM_NODES (2)

#define INIT 0
#define STARTUP_TIME (0.0)

int ScheduleNewEvent(int, double, int, char *, int);

int GetEvent(int *, double *, int *, char *, int *);

void ProcessEvent(unsigned int, double, int, void *, unsigned int, void *ptr);

uint32_t *get_seed1_ptr(unsigned int);

uint32_t *get_seed2_ptr(unsigned int);

double Random(uint32_t *, uint32_t *);
double Expent(double, uint32_t *, uint32_t *);

// the below max values can be modified with no problem
// for handling very huge hardware patforms
#define MAX_NUMA_NODES 128
#define MAX_CPUS_PER_NODE 1024

#define NUMA_BALANCING
// #define BENCHMARKING
// #define DEBUG

// int get_current(void);
// double get_current_time(void);
int get_NUMAnode(void);
int get_totNUMAnodes(void);
int *getcounter(void);
int *getmin(void);
int *getmax(void);

#define UPDATE(addr, val) *addr = val //; *((char*)addr + MAX_SIZE) = val TO BE FIXED

#endif
