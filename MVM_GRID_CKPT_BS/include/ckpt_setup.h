#ifndef _CKPT_SETUP_
#define _CKPT_SETUP_

#include <stdint.h>

#ifndef MOD
#define MOD 8
#endif

#ifndef ALLOCATOR_AREA_SIZE
#define ALLOCATOR_AREA_SIZE 0x200000UL
#endif

#define BITMAP_SIZE (ALLOCATOR_AREA_SIZE / MOD) / 8 + 1

void _tls_setup();

void _restore_area(uint8_t *);

void _set_ckpt(uint8_t *);

#endif