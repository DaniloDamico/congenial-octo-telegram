#ifndef _HEAD_
#define _HEAD_

#define PAGE 4096
#define SIZE                                                                   \
    (PAGE << 4) // actual size of the head arrays with instructions, patches and
                // code map

#define mask (0xfffffffffffff000) // generic page alignment mask

void setup_memory_access_rules(void);

void the_patch_assembly(void);

void ckpt_assembly(void);
void dummy_ckpt(void);

void the_patch(unsigned long, unsigned long);

void _instructions(void);

void _patches(void);

void _codemap(void);

#ifndef VERBOSE
#define AUDIT if (0)
#else
#define AUDIT if (1)
#endif

#endif
