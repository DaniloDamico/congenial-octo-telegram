#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "elf_parse.h"
#include "memory.h"

void the_patch(unsigned long, unsigned long) __attribute__((used));

// the_patch(...) is the default function offered by MVM for instrumenting
// whatever memory load/store instruction it can be activated by activating the
// ASM_PREAMBLE macro in src/_elf_parse.c this function is general purpose and
// is executed right before the memory load/store it passes through C
// programming hence it has the intrinsic cost of CPU snapshot save/restore
// to/from the stack when taking control or passing it back this function takes
// the pointers to the instruction metadata and CPU snapshot

void the_patch(unsigned long mem, unsigned long regs) {
    instruction_record *instruction = (instruction_record *)mem;
    target_address *target;
    unsigned long A = 0, B = 0;
    uint8_t *address;

    // get the address
    if (instruction->effective_operand_address != 0x0) {
        address = (uint8_t *)instruction->effective_operand_address;
    } else {
        target = &(instruction->target);
        memcpy((char *)&A, (char *)(regs + 8 * (target->base_index - 1)), 8);
        if (target->scale_index) {
            memcpy((char *)&B, (char *)(regs + 8 * (target->scale_index - 1)), 8);
        }
        address = (uint8_t *)((long)target->displacement + (long)A + (long)((long)B * (long)target->scale));
    }
    ckpt_chunk(address);
}

// used_defined(...) is the real body of the user-defined instrumentation
// process, all the stuff you put here represents the actual execution path of
// an instrumented instruction given that you have the exact representation of
// the instruction to be instrumented, you can produce the block of ASM level
// instructions to be really used for istrumentation clearly any memory/register
// side effect is under the responsibility of the patch programme the
// instrumentation instructions whill be executed right after the original
// instruction to be instrumented

#define buffer                                                                                                         \
    user_defined_buffer // this avoids compile-time replication of the buffer
                        // symbol
char buffer[1024];
// in this function you get the pointer to the metedata representaion of the
// instruction to be instrumented and the pointer to the buffer where the patch
// (namely the instrumenting instructions) can be placed simply eturning form
// this function with no management of the pointed areas menas that you are
// skipping the instrumentatn of this instruction

void user_defined(instruction_record *actual_instruction, patch *actual_patch) {
    // not used in this patch
}
