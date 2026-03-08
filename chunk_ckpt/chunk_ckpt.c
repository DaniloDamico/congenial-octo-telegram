#include <asm/prctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "memory.h"
#include "setup.h"

#ifdef DEBUG
void verify_restored_area(uint8_t *, uint8_t *);
uint8_t *shadow_area[OBJECTS] = {NULL};
#endif

typedef struct _seeds {
    uint32_t seed1; // seed passed in input to randomization functions
    uint32_t seed2; // seed passed in input to randomization functions
} seeds;

extern uint32_t *seeds1[OBJECTS];
extern uint32_t *seeds2[OBJECTS];

seeds object_seeds[OBJECTS];

void save_seeds(int object) {
    object_seeds[object].seed1 = *seeds1[object];
    AUDIT printf("object %d - saving seed1 %d\n", object, object_seeds[object].seed1);
    object_seeds[object].seed2 = *seeds2[object];
    AUDIT printf("object %d - saving seed2 %d\n", object, object_seeds[object].seed2);
}

void restore_seeds(int object) {
    *seeds1[object] = object_seeds[object].seed1;
    AUDIT printf("object %d - restore seed1 %d\n", object, *seeds1[object]);
    *seeds2[object] = object_seeds[object].seed2;
    AUDIT printf("object %d - restore seed2 %d\n", object, *seeds2[object]);
}

void restore_object(int object) {
    restore_allocator(object);
    restore_chunks(object);
    restore_seeds(object);
#ifdef DEBUG
    uint8_t *area = (uint8_t *)(8 * (1024 * MAX_MEMORY) + object * (2 * MAX_MEMORY * MEM_NODES));
    if (memcmp(shadow_area[object], area, MAX_MEMORY)) {
        printf("ERROR: object %d restore failed\n", object);
        fflush(stdout);
        verify_restored_area(area, shadow_area[object]);
        exit(EXIT_FAILURE);
    }
#endif
}

void set_ckpt(int object) {
    save_seeds(object);
    set_allocator_ckpt(object);
#ifdef DEBUG
    uint8_t *area = (uint8_t *)(8 * (1024 * MAX_MEMORY) + object * (2 * MAX_MEMORY * MEM_NODES));
    if (shadow_area[object] == NULL) {
        shadow_area[object] = mmap(NULL, MAX_MEMORY, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
        if (shadow_area[object] == MAP_FAILED) {
            printf("object %d - shadow_area mmap failed\n", object);
            exit(EXIT_FAILURE);
        }
    }
    memcpy(shadow_area[object], area, MAX_MEMORY);
    if (memcmp(shadow_area[object], area, MAX_MEMORY)) {
        printf("object %d - set ckpt failed\n", object);
        exit(EXIT_FAILURE);
    }
#endif
#ifdef CHUNK_FULL
    set_used_chunks_ckpt(object);
#endif
}

#ifdef DEBUG
void verify_restored_area(uint8_t *area, uint8_t *area_copy) {
    for (int offset = 0; offset < MAX_MEMORY; offset += 32) {
        if (memcmp((void *)(area + offset), (void *)(area_copy + offset), 32)) {
            printf("Checkpoint verify failed:\n"
                   "Offset: %d\n",
                   offset);
            exit(EXIT_FAILURE);
        }
    }
}
#endif