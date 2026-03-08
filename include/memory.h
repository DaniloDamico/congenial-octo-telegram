#ifndef MEMORY_H
#define MEMORY_H

void allocators_base_init(void);

void object_allocator_setup(void);

void set_allocator_ckpt(int);

void restore_allocator(int);

void restore_chunks(int);

void ckpt_chunk(void *);

void set_used_chunks_ckpt(int);

#endif