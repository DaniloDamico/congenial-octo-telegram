#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "elf_parse.h"
#include "head.h"

void user_defined(instruction_record *, patch *);

uint64_t asl_randomization = 0;

char *functions[TARGET_FUNCTIONS];

char buffer[LINE_SIZE];
char prev_line[LINE_SIZE];
char temp_line[LINE_SIZE]; // this is for RIP-relative instruction management

void (*address)(void) = _instructions;
instruction_record *instructions;
int target_instructions = 0;

void (*address1)(void) = _patches;
patch *patches; // pointer to the memory block where the patch is built

uint64_t intermediate_zones[SIZE];
uint64_t intermediate_flags[SIZE];
int intermediate_zones_index = -1;

void audit_block(instruction_record *the_record) {
    if (the_record->type == 'l') { // avoid audit of load instruction in ckpt
        return;
    }
    printf("instruction record:\n \
			belonging function is %s\n \
			address is %p\n \
			size is %lu\n \
			type (l/s) is %c\n \
			rip relative (y/n) is %c\n \
			effective operand address is %p\n \
			requires indirect jump (y/n) is %c\n \
			indirect jump buffer location is %p\n \
			operation is %s\n \
			source is %s\n \
			destination is %s\n \
			data size is %d\n \
			number of instrumentation instructions %d\n",
           (char *)the_record->function, (void *)the_record->address,
           the_record->size, the_record->type, the_record->rip_relative,
           (void *)the_record->effective_operand_address,
           the_record->indirect_jump, (void *)the_record->middle_buffer,
           the_record->op, the_record->source, the_record->dest,
           the_record->data_size, the_record->instrumentation_instructions);
    fflush(stdout);
}

char intermediate[LINE_SIZE];
int fd;
// this function is essentially a trampoline for passing control to the
// user-defined instrumentation function
void build_intermediate_representation(void) {

    int i;
    int ret;

    patches = (patch *)
        address1; // always use this reference for accessing the patch area

    for (i = 0; i < target_instructions; i++) {

        patches[i].functional_instr_size = 0;

        // just passing through user-defined stuff
        user_defined(&instructions[i], &patches[i]);
    }
}

uint64_t book_intermediate_target(uint64_t instruction_address,
                                  unsigned long instruction_size) {

    int i;

    for (i = 0; i < intermediate_zones_index; i++) {
        if ((intermediate_zones[i] >=
             (instruction_address + instruction_size) - 128) &&
            (intermediate_zones[i] <=
             (instruction_address + instruction_size) + 127) &&
            (intermediate_flags[i] == 0)) {
            intermediate_flags[i] = 1;
            AUDIT
            printf(
                "found free intermediate zone at index %d - making it busy\n",
                i);
            return intermediate_zones[i];
        }
    }
    return 0x0;
}

void build_patches(void) {

    int i;
    unsigned long size;
    uint64_t instruction_address;
    int jmp_displacement;
    char *jmp_target;
    char v[128]; // this hosts the jmp binary
    int jmp_back_displacement;
    uint64_t patch_address;
    int pos;
    uint64_t effective_operand_address;
    uint64_t effective_operand_displacement;
    uint64_t intermediate_target;

    uint64_t test_code = (uint64_t)the_patch_assembly;
    int test_code_size = 82; // this is taken from the compiled version of the
                             // src/_asm_patch.S file

    patches = (patch *)address1;

    for (i = 0; i < target_instructions; i++) {
        if (instructions[i].type == 'l') {
            continue; // avoid patch of load instruction
        }

        // saving original instruction address
        instruction_address = patches[i].original_instruction_address =
            instructions[i].address;

        // saving original instruction size
        size = patches[i].original_instruction_size = instructions[i].size;

        // saving the original instruction bytes
        memcpy((char *)(patches[i].original_instruction_bytes),
               (char *)(instructions[i].address), size);

        patches[i].code = patches[i].block; // you can put wathever instruction
                                            // in the block of the patch

#ifdef ASM_PREAMBLE
        // copy the asm-patch code into the instructions block
        memcpy((char *)(patches[i].code), (char *)(test_code), test_code_size);
        // adjust the asm-patch offset for the call to the patch function
        jmp_target = (char *)the_patch;
        jmp_displacement =
            (int)((char *)jmp_target -
                  ((char *)(patches[i].code) +
                   50)); // this is because we substitute the original call
                         // instruction offset with the one observable after the
                         // asm-patch instructions copy
        pos = 0;
        v[pos++] = 0xe8;
        v[pos++] = (unsigned char)(jmp_displacement & 0xff);
        v[pos++] = (unsigned char)(jmp_displacement >> 8 & 0xff);
        v[pos++] = (unsigned char)(jmp_displacement >> 16 & 0xff);
        v[pos++] = (unsigned char)(jmp_displacement >> 24 & 0xff);
        // memcpy((char*)(patches[i].code) + 36,v,5);

        // CAREFULL THIS
        memcpy((char *)(patches[i].code) + 45, v, 5);

        // adjust the parameter to be passed by the asm-patch to the actual
        // patch
        pos = 0;
        v[pos++] = (unsigned char)((unsigned long)&(instructions[i]) & 0xff);
        v[pos++] =
            (unsigned char)((unsigned long)&(instructions[i]) >> 8 & 0xff);
        v[pos++] =
            (unsigned char)((unsigned long)&(instructions[i]) >> 16 & 0xff);
        v[pos++] =
            (unsigned char)((unsigned long)&(instructions[i]) >> 24 & 0xff);
        v[pos++] =
            (unsigned char)((unsigned long)&(instructions[i]) >> 32 & 0xff);
        v[pos++] =
            (unsigned char)((unsigned long)&(instructions[i]) >> 40 & 0xff);
        v[pos++] =
            (unsigned char)((unsigned long)&(instructions[i]) >> 48 & 0xff);
        v[pos++] =
            (unsigned char)((unsigned long)&(instructions[i]) >> 56 & 0xff);

        memcpy((char *)(patches[i].code) + 37, v, 8);
        patches[i].code = patches[i].code + test_code_size;
#endif
        // copy the original memory access instruction to be executed
        memcpy((char *)(patches[i].code), (char *)(instructions[i].address),
               size);

#ifdef ASM_PREAMBLE
        // move again at the begin of the block of instructions forming the
        // patch NOTE: you will need to have patches[i].code point again to
        // patches[i].block before proceeding with the following if/else
        patches[i].code = patches[i].code - test_code_size;
#endif

        if (size >= 5) {
            AUDIT
            printf("packing a 5-byte jump for instruction with index %d\n", i);
            jmp_target = (char *)(patches[i].code);
            jmp_displacement =
                (int)((char *)jmp_target -
                      ((char *)(instruction_address) +
                       5)); // this is because we substitute the original
                            // instruction with a 5-byte relative jmp
            AUDIT
            printf("jump displacement is %d\n", jmp_displacement);
            fflush(stdout);
            pos = 0;
            v[pos++] = 0xe9;
            v[pos++] = (unsigned char)(jmp_displacement & 0xff);
            v[pos++] = (unsigned char)(jmp_displacement >> 8 & 0xff);
            v[pos++] = (unsigned char)(jmp_displacement >> 16 & 0xff);
            v[pos++] = (unsigned char)(jmp_displacement >> 24 & 0xff);
            // record the patch to be applied
            memcpy((char *)(patches[i].jmp_to_post), v, 5);
        } else {
            intermediate_target =
                book_intermediate_target(instruction_address, size);
            if (intermediate_target == 0x0) {
                printf("no intermediate target available for the mov "
                       "instruction at runtime address %p index is %d\n",
                       (void *)instruction_address, i);
                fflush(stdout);
                exit(EXIT_FAILURE);
            } else {
                AUDIT
                printf("intermediate target available at address %p for the "
                       "mov instruction at runtime address %p\n",
                       (void *)intermediate_target,
                       (void *)instruction_address);
                instructions[i].middle_buffer = intermediate_target;
                jmp_target = (char *)(patches[i].code);
                jmp_displacement =
                    (int)((char *)jmp_target -
                          ((char *)(intermediate_target) +
                           5)); // this is because we substite the original
                                // cli/nop instruction set with a 5-byte
                                // relative jmp from the intermediate area
                AUDIT
                printf("jump displacement from intermediate target is %d\n",
                       jmp_displacement);
                fflush(stdout);
                pos = 0;
                v[pos++] = 0xe9;
                v[pos++] = (unsigned char)(jmp_displacement & 0xff);
                v[pos++] = (unsigned char)(jmp_displacement >> 8 & 0xff);
                v[pos++] = (unsigned char)(jmp_displacement >> 16 & 0xff);
                v[pos++] = (unsigned char)(jmp_displacement >> 24 & 0xff);
                // BUG FIXING WITH THE BELOW MEMCPY
                memcpy((char *)(patches[i].jmp_to_post), v, 5);
                patches[i].intermediate_zone_address = intermediate_target;

                // we now prepare the jump to the intermediate zone for this
                // instruction - this becomes the real block of instructions
                // substituting the original mov instruction
                jmp_target = (char *)(intermediate_target);
                jmp_displacement =
                    (signed char)((char *)jmp_target -
                                  ((char *)(instruction_address) +
                                   2)); // this is because we substite the
                                        // original instrucion with a 2-byte
                                        // relative jmp
                AUDIT
                if ((char *)jmp_target < ((char *)(instruction_address) + 2)) {
                    printf("negative intermediate jump displacement for "
                           "instruction %d\n",
                           i);
                }
                pos = 0;
                v[pos++] = 0xeb;
                v[pos++] = (unsigned char)(jmp_displacement & 0xff);
                memcpy((char *)(patches[i].jmp_to_intermediate), v, 2);
            }
        }

#ifdef ASM_PREAMBLE
        // NOTE: for the below code fragment you will need to have
        // patches[i].code point to the copy of the original instruction - you
        // will need to step forward other preceeding instructions forming the
        // patch
        patches[i].code = patches[i].code + test_code_size;
#endif

        memcpy(patches[i].code + size, patches[i].functional_instr,
               patches[i].functional_instr_size);

        jmp_target = (char *)(instruction_address) + size;

        size += patches[i].functional_instr_size;

        jmp_back_displacement =
            (char *)jmp_target - ((char *)(patches[i].code) + size +
                                  5); // here we go beyond the instruction+jmp -
                                      // but this is a baseline
        AUDIT
        printf("jump back displacement is %d\n", jmp_back_displacement);
        fflush(stdout);
        pos = 0;
        v[pos++] = 0xe9;
        v[pos++] = (unsigned char)(jmp_back_displacement & 0xff);
        v[pos++] = (unsigned char)(jmp_back_displacement >> 8 & 0xff);
        v[pos++] = (unsigned char)(jmp_back_displacement >> 16 & 0xff);
        v[pos++] = (unsigned char)(jmp_back_displacement >> 24 & 0xff);
        // log the jmp-back into the patch area
        memcpy((char *)(patches[i].code) + size, (char *)v, 5);

        size -= patches[i].functional_instr_size;

        if (instructions[i].rip_relative ==
            'y') { // in the patch we need to change the actual offset to be
                   // applied to RIP
            effective_operand_address =
                instructions[i]
                    .effective_operand_address; // this is based on the offset
                                                // from the original RIP
            effective_operand_displacement =
                effective_operand_address - (uint64_t)(patches[i].code + size);
            AUDIT
            printf("effective operand displacement is %p\n",
                   (void *)effective_operand_displacement);

            patches[i].code[size - 1] =
                (unsigned char)(effective_operand_displacement >> 24 & 0xff);
            patches[i].code[size - 2] =
                (unsigned char)(effective_operand_displacement >> 16 & 0xff);
            patches[i].code[size - 3] =
                (unsigned char)(effective_operand_displacement >> 8 & 0xff);
            patches[i].code[size - 4] =
                (unsigned char)(effective_operand_displacement & 0xff);
        }
    }
}

void apply_patches(void) {

    int i;
    unsigned long size;
    unsigned long instruction_address;
    unsigned long instruction_patch;
    unsigned short instruction_short_patch;

    for (i = 0; i < target_instructions; i++) {
        if (instructions[i].type == 'l') {
            continue; // avoid apply patch to load instructions
        }
        size = instructions[i].size;
        instruction_address = instructions[i].address;
        AUDIT
        printf("applying a patch to instruction with index %d\n", i);
        fflush(stdout);
        if (size >= 5) {
            instruction_patch = (unsigned long)patches[i].jmp_to_post;
            syscall(10, instruction_address & mask, PAGE,
                    PROT_READ | PROT_EXEC | PROT_WRITE);
            syscall(10, (instruction_address & mask) + PAGE, PAGE,
                    PROT_READ | PROT_EXEC | PROT_WRITE);
            memcpy((char *)instruction_address, (char *)instruction_patch, 5);
            // original permissions need to be restored - TO DO
        } else {
            syscall(10, (instruction_address & mask) - 128, PAGE,
                    PROT_READ | PROT_EXEC | PROT_WRITE);
            syscall(10, instruction_address & mask, PAGE,
                    PROT_READ | PROT_EXEC | PROT_WRITE);
            syscall(10, (instruction_address & mask) + PAGE, PAGE,
                    PROT_READ | PROT_EXEC | PROT_WRITE);
            memcpy((char *)instruction_address,
                   (char *)patches[i].jmp_to_intermediate, 2);
            memcpy((char *)patches[i].intermediate_zone_address,
                   (char *)patches[i].jmp_to_post, 5);
            AUDIT
            printf("patch applied to instruction with index %d - intermediate "
                   "jump required\n",
                   i);
        }
    }
}

// the index returned by this function depends on how CPU registes are
// saved into the stack area when the memory access patch gets executed
// this depends on src/_asm_patches.S
int get_register_index(char *the_register) {

    if (strcmp(the_register, "%rax") == 0) {
        return 11;
    }
    if (strcmp(the_register, "%rdx") == 0) {
        return 13;
    }
    return -2;
}

// this function determines the size of touched data based on the instruction
// source/destination type is either 'l' or 's' for load/store instructions it i
// usefull for mov instructions where data size is not explicit
int operands_check(char *source, char *destination, char type) {

    char *reg = (type == 's') ? source : destination;
    if (!strcmp(reg, "%eax") || !strcmp(reg, "%ebx") || !strcmp(reg, "%ecx") ||
        !strcmp(reg, "%edx") || !strcmp(reg, "%r8d") || !strcmp(reg, "%r9d") ||
        !strcmp(reg, "%r10d") || !strcmp(reg, "%r11d") ||
        !strcmp(reg, "%r12d") || !strcmp(reg, "%r13d") ||
        !strcmp(reg, "%r14d") || !strcmp(reg, "%r15d") ||
        !strcmp(reg, "%esi") || !strcmp(reg, "%edi")) {
        return sizeof(int);
    }

    if (!strcmp(reg, "%ax") || !strcmp(reg, "%bx") || !strcmp(reg, "%cx") ||
        !strcmp(reg, "%dx") || !strcmp(reg, "%r8w") || !strcmp(reg, "%r9w") ||
        !strcmp(reg, "%r10w") || !strcmp(reg, "%r11w") ||
        !strcmp(reg, "%r12w") || !strcmp(reg, "%r13w") ||
        !strcmp(reg, "%r14w") || !strcmp(reg, "%r15w") || !strcmp(reg, "%si") ||
        !strcmp(reg, "%di")) {
        return 2;
    }

    if (!strcmp(reg, "%al") || !strcmp(reg, "%bl") || !strcmp(reg, "%cl") ||
        !strcmp(reg, "%dl") || !strcmp(reg, "%r8b") || !strcmp(reg, "%r9b") ||
        !strcmp(reg, "%r10b") || !strcmp(reg, "%r11b") ||
        !strcmp(reg, "%r12b") || !strcmp(reg, "%r13b") ||
        !strcmp(reg, "%r14b") || !strcmp(reg, "%r15b") ||
        !strcmp(reg, "%sil") || !strcmp(reg, "%dil")) {
        return sizeof(char);
    }

    return sizeof(unsigned long); // this is the 8-byte default
}

// this function returns the number of bytes to be touched by a given
// instruction
int get_data_size(char *instruction, char *source, char *dest, char type) {

    if (!strcmp(instruction, "movb")) {
        return sizeof(char);
    }
    if (!strcmp(instruction, "movl")) {
        return sizeof(int);
    }
    if (!strcmp(instruction, "movq")) {
        return sizeof(unsigned long);
    }

    if (!strcmp(instruction, "movss")) {
        return sizeof(float);
    }
    if (!strcmp(instruction, "movsd")) {
        return sizeof(double);
    };

    if (!strcmp(instruction, "movzwl")) {
        return sizeof(float);
    };
    if (!strcmp(instruction, "movzbl")) {
        return sizeof(float);
    };
    if (!strcmp(instruction, "movzbw")) {
        return sizeof(int);
    };

    if (!strcmp(instruction, "movsbl")) {
        return sizeof(int);
    };

    if (!strcmp(instruction, "mov")) {
        return operands_check(source, dest, type);
    }

    return -1; // unknown instruction
}

// this returns the number of memory move instructions that have been identified
// for instrumentation - this number is also written to the target_instructions
// variable
int elf_parse(char **function_names, char *parsable_elf) {

    int i;
    int j;
    int k;
    int num_functions;
    int offset;
    int len;
    FILE *the_file;
    char *guard;
    char *p;
    char *aux;
    char category;
    char rip_relative;
    uint64_t function_start_address;
    uint64_t function_end_address;
    uint64_t instruction_start_address;
    unsigned long instruction_len;
    uint64_t rip_displacement;
    unsigned long target_displacement;
    int register_index;
    long corrector;

    instructions = (instruction_record *)address;

    for (i = 0;; i++) { // counting functions to parse
        if (function_names[i] == NULL) {
            break;
        }
    }
    num_functions = i;

    AUDIT
    printf("number of functions to parse: %d\n", num_functions);
    fflush(stdout);

    the_file = fopen(parsable_elf, "r");
    if (!the_file) {
        printf("%s: disassembly file %s not accessible\n", VM_NAME,
               parsable_elf);
        fflush(stdout);
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < num_functions; i++) { // parsing all the functions
        AUDIT
        printf("sarching for function %s\n", function_names[i]);
        offset = fseek(the_file, 0,
                       SEEK_SET); // moving to the beginning of the ELF file

        while (1) {
            guard = fgets(buffer, LINE_SIZE, the_file);
            if (guard == NULL) {
                AUDIT
                printf("ending cycle for string %s\n", function_names[i]);
                printf("function to be instrumented %s not found in the "
                       "executable\n",
                       function_names[i]);
                exit(EXIT_FAILURE);
                break;
            }
            strtok(buffer, "\n");
            if (strstr(buffer, function_names[i])) {
                AUDIT
                printf("found line for function %s\n", function_names[i]);
                strtok(buffer, " ");
                AUDIT
                printf("address of function %s is %s\n", function_names[i],
                       buffer);
                function_start_address =
                    (unsigned long)strtol(buffer, NULL, 16);
                AUDIT
                printf("numerical address of function %s is %p\n",
                       function_names[i], (void *)function_start_address);

                while (1) {
                    memcpy(prev_line, buffer, LINE_SIZE);
                    guard = fgets(buffer, LINE_SIZE, the_file);
                    AUDIT
                    printf("%s", buffer);

                    if (strcmp(buffer, "\n") == 0) {
                        AUDIT
                        printf("found end of function %s\n", function_names[i]);
                        strtok(prev_line, ":");
                        AUDIT
                        printf("end address of function %s is %s\n",
                               function_names[i], prev_line);
                        function_end_address =
                            (unsigned long)strtol(prev_line, NULL, 16);
                        AUDIT
                        printf("numerical end address of function %s is %p\n",
                               function_names[i], (void *)function_end_address);
                        break;
                    }
                    // now we look at the actual instruction
                    strtok(buffer, "\n");
                    if (strstr(buffer, "mov") && ((strstr(buffer, ")"))) &&
                        (!(strstr(buffer, "(%rbp,"))) &&
                        (!(strstr(buffer, "(%rsp,"))) &&
                        (!(strstr(buffer, "(%rbp)"))) &&
                        (!(strstr(buffer, "(%rsp)")))) {
                        AUDIT
                        printf(
                            "found target data move instruction - line is %s\n",
                            buffer);

                        if (strstr(buffer, "),")) {
                            AUDIT
                            printf("move from memory (load)\n");
                            category = 'l';
                            continue;
                        } else {
                            AUDIT
                            printf("move to memory (store)\n");
                            category = 's';
                        }

                        if (strstr(buffer, "%rip")) {
                            AUDIT
                            printf("is rip relative\n");
                            rip_relative = 'y';
                        } else {
                            AUDIT
                            printf("is not rip relative\n");
                            rip_relative = 'n';
                        }
                        strtok(buffer, "#");
                        // at this point the instruction line has been removed
                        // of \n and # so that we can tokenize again having
                        // already excluded instruction comments
                        AUDIT
                        printf(
                            "found target data move instruction - line is %s\n",
                            buffer);
                        p = strtok(buffer, ":\t");

                        instruction_start_address =
                            strtol(p, NULL, 16) + asl_randomization;
                        AUDIT
                        printf("instruction is in function %s\n",
                               function_names[i]);
                        instructions[target_instructions].function =
                            function_names[i];
                        AUDIT
                        printf("instruction address is %p\n",
                               (void *)instruction_start_address);
                        instructions[target_instructions].address =
                            instruction_start_address;
                        // we now simply rewrite 0x0 on the structure that keeps
                        // the targeted memory location information
                        memset(
                            (char *)&(instructions[target_instructions].target),
                            0x0, sizeof(target_address));

                        p = strtok(NULL, ":\t");
                        AUDIT
                        printf("instruction binary is %s\n", p);
                        instruction_len = 0;
                        for (j = 0; j < strlen(p); j++) {
                            if (p[j] != ' ') {
                                instruction_len++;
                            }
                        }
                        instruction_len >>= 1;
                        AUDIT
                        printf("instruction len is %lu\n", instruction_len);
                        instructions[target_instructions].size =
                            instruction_len;
                        if (instruction_len < 5) {

                            instructions[target_instructions].indirect_jump =
                                'y';
                        } else {
                            instructions[target_instructions].indirect_jump =
                                'n';
                        }
                        instructions[target_instructions].middle_buffer = 0x0;
                        p = strtok(NULL, ":\t");
                        if (!strstr(p, "mov")) {
                            printf("parsing bug, expected 'mov' not foud\n");
                            exit(EXIT_FAILURE);
                        }
                        AUDIT
                        printf("%s\n",
                               p); // printing the whole instruction string
                        strcpy(instructions[target_instructions].whole, p);
                        if (category == 'l') {
                            aux = strstr(p, "),");
                            *(aux + 1) = ' ';

                        } else {
                            aux = strstr(p, ",");
                            *(aux) = ' ';
                        }

                        // is it a load or a store?
                        instructions[target_instructions].type = category;
                        instructions[target_instructions].rip_relative =
                            rip_relative;

                        p = strtok(p, " ");
                        AUDIT
                        printf("%s\n", p); // printing mov*
                        strcpy(instructions[target_instructions].op, p);

                        p = strtok(NULL, " ");
                        AUDIT
                        printf("%s\n", p); // priting sorce
                        strcpy(instructions[target_instructions].source, p);

                        p = strtok(NULL, " ");
                        AUDIT
                        printf("%s\n", p); // priting dest
                        strcpy(instructions[target_instructions].dest, p);

                        instructions[target_instructions].data_size = -1;
                        // determining the size of mouved data
                        instructions[target_instructions].data_size =
                            get_data_size(
                                instructions[target_instructions].op,
                                instructions[target_instructions].source,
                                instructions[target_instructions].dest,
                                instructions[target_instructions].type);

                        // if(!strcmp(instructions[target_instructions].op,"movb"))
                        // instructions[target_instructions].data_size = 1;

                        instructions[target_instructions]
                            .effective_operand_address =
                            0x0; // this is the default
                        // need to fix the rip relative to absolute operand
                        // address before ending
                        if (rip_relative == 'y' && category == 'l') {
                            strcpy(temp_line,
                                   instructions[target_instructions].source);
                        }
                        if (rip_relative == 'y' && category == 's') {
                            strcpy(temp_line,
                                   instructions[target_instructions].dest);
                        }
                        if (rip_relative == 'y') {
                            strtok(temp_line, "(");

                            if (temp_line[0] == '-') {
                                rip_displacement =
                                    strtoul(temp_line + 1, NULL, 16);
                                corrector = -1;
                            } else {
                                rip_displacement = strtoul(temp_line, NULL, 16);
                                corrector = 1;
                            }
                            corrector = corrector * (long)rip_displacement;
                            AUDIT
                            printf("rip displacement is %p\n",
                                   (void *)rip_displacement);
                            AUDIT
                            printf("displacement value is %ld\n", corrector);
                            instructions[target_instructions]
                                .effective_operand_address =
                                (unsigned long)((long)((char *)
                                                           instruction_start_address +
                                                       instruction_len) +
                                                corrector);
                            AUDIT
                            printf("effective operand address is %lu\n",
                                   instructions[target_instructions]
                                       .effective_operand_address);

                        } else {
                            AUDIT
                            printf("computing the parameters for the operand "
                                   "address\n");
                            fflush(stdout);

                            instructions[target_instructions]
                                .effective_operand_address = 0x0;
                            if (category == 'l') {
                                strcpy(
                                    temp_line,
                                    instructions[target_instructions].source);
                            }
                            if (category == 's') {
                                strcpy(temp_line,
                                       instructions[target_instructions].dest);
                            }
                            if (temp_line[0] != '(') {
                                AUDIT
                                printf("found a displacement\n");
                                fflush(stdout);
                                strtok(temp_line, "(");
                                if (temp_line[0] == '-') {
                                    target_displacement =
                                        strtoul(temp_line + 1, NULL, 16);
                                    corrector = -1;
                                } else {
                                    target_displacement =
                                        strtoul(temp_line, NULL, 16);
                                    corrector = 1;
                                }
                                instructions[target_instructions]
                                    .target.displacement =
                                    (long)(target_displacement) *
                                    (long)corrector;
                                p = aux =
                                    strtok(NULL, "("); // go to the next part of
                                                       // the address expression
                                strtok(p, ",)");
                                k = 0;
                                do {
                                    aux = strtok(NULL,
                                                 ",)"); // count the arguments
                                    k++;
                                } while (aux);

                                switch (k) {
                                case 1:
                                    register_index = get_register_index(p);
                                    AUDIT
                                    printf("register index for register %s is "
                                           "%d\n",
                                           p, register_index);
                                    instructions[target_instructions]
                                        .target.base_index = register_index;
                                    break;
                                case 2:
                                    break;
                                case 3:

                                    register_index = get_register_index(p);
                                    AUDIT
                                    printf("register index for base register "
                                           "%s is %d\n",
                                           p, register_index);
                                    instructions[target_instructions]
                                        .target.base_index = register_index;
                                    fflush(stdout);
                                    p += strlen(p) + 1;
                                    register_index = get_register_index(p);
                                    AUDIT
                                    printf("register index for scale register "
                                           "%s is %d\n",
                                           p, register_index);
                                    instructions[target_instructions]
                                        .target.scale_index = register_index;
                                    p += strlen(p) + 1;
                                    AUDIT
                                    printf("scale is %s\n", p);
                                    fflush(stdout);
                                    instructions[target_instructions]
                                        .target.scale =
                                        (unsigned long)strtol(p, NULL, 16);
                                    break;
                                }
                            } else {
                                AUDIT
                                printf("no displacement found\n");
                                fflush(stdout);
                                p = aux = strtok(temp_line, "(,)");
                                k = 0;
                                do {
                                    aux = strtok(NULL,
                                                 "(,)"); // cont the arguments
                                    k++;
                                } while (aux);

                                switch (k) {
                                case 1:
                                    register_index = get_register_index(p);
                                    AUDIT
                                    printf("register index for register %s is "
                                           "%d\n",
                                           p, register_index);
                                    fflush(stdout);
                                    instructions[target_instructions]
                                        .target.base_index = register_index;
                                    break;
                                case 2:
                                    break;
                                case 3:
                                    register_index = get_register_index(p);
                                    AUDIT
                                    printf("register index for base register "
                                           "%s is %d\n",
                                           p, register_index);
                                    instructions[target_instructions]
                                        .target.base_index = register_index;
                                    fflush(stdout);
                                    p += strlen(p) + 1;
                                    register_index = get_register_index(p);
                                    AUDIT
                                    printf("register index for scale register "
                                           "%s is %d\n",
                                           p, register_index);
                                    instructions[target_instructions]
                                        .target.scale_index = register_index;
                                    p += strlen(p) + 1;
                                    AUDIT
                                    printf("scale is %s\n", p);
                                    fflush(stdout);
                                    instructions[target_instructions]
                                        .target.scale =
                                        (unsigned long)strtol(p, NULL, 16);
                                    break;
                                }
                            }
#ifdef COMPUTE_ADDRESS
                            strtok(temp_line, "(");
                        already_tokenized:
                            p = strtok(NULL, "(");
                            if (p[0] != ',') {
                                // we have the base register
                                strtok(p, ",)");
                                register_index = get_register_index(p);
                                AUDIT
                                printf("register index for register %s is %d\n",
                                       p, register_index);
                            } else {
                                strtok(p, ",)");
                            }
#endif
                        }

                        instructions[target_instructions]
                            .instrumentation_instructions = 0;
                        instructions[target_instructions].record_index =
                            target_instructions;

                        target_instructions++;

                        if (target_instructions > NUM_INSTRUCTIONS) {
                            printf("%s: out of head-memory\n", VM_NAME);
                            fflush(stdout);
                            exit(EXIT_FAILURE);
                        }
                    }
                }
                break;
            }
        }
    }

    return target_instructions;
}

unsigned long find_elf_parse_compile_time_address(char *parsable_elf) {

    FILE *the_file;
    char *guard;
    unsigned long function_start_address;
    long corrector;

    AUDIT
    printf("finding elf_parse compile time address\n");

    the_file = fopen(parsable_elf, "r");
    if (!the_file) {
        printf("%s: disassembly file %s not accessible\n", VM_NAME,
               parsable_elf);
        fflush(stdout);
        exit(EXIT_FAILURE);
    }

    while (1) {
        guard = fgets(buffer, LINE_SIZE, the_file);
        if (guard == NULL) {
            break;
        }
        strtok(buffer, "\n");
        if (strstr(buffer, "elf_parse")) {
            AUDIT
            printf("found line for function elf_parse\n");
            strtok(buffer, " ");
            AUDIT
            printf("address of function elf_parse is %s\n", buffer);
            function_start_address = (unsigned long)strtol(buffer, NULL, 16);
            AUDIT
            printf("numerical address of function elf_parse is %p\n",
                   (void *)function_start_address);
            fclose(the_file);
            return function_start_address;
        }
    }

    fclose(the_file);
    return 0x0;
}

void find_intermediate_zones(char *parsable_elf) {

    FILE *the_file;
    char *guard;
    unsigned long function_start_address;
    long corrector;

    AUDIT
    printf("finding intermediate zones\n");

    the_file = fopen(parsable_elf, "r");
    if (!the_file) {
        printf("%s: disassembly file %s not accessible\n", VM_NAME,
               parsable_elf);
        fflush(stdout);
        exit(EXIT_FAILURE);
    }

    while (1) {
        guard = fgets(buffer, LINE_SIZE, the_file);
        if (guard == NULL) {
            break;
        }
        strtok(buffer, "\n");
        if (strstr(buffer, "<_wrap_main>:")) {
            goto out;
        }
        if (strstr(buffer, "\tcli")) {
            AUDIT
            printf("found line with the cli instruction\n");
            strtok(buffer, ":");
            AUDIT
            printf("compile time address of a cli instruction is %s\n", buffer);
            intermediate_zones[++intermediate_zones_index] =
                ( // unsigned long
                    uint64_t)strtol(buffer, NULL, 16) +
                asl_randomization;
            AUDIT
            printf("runtime time address of the cli instruction is %p\n",
                   (void *)intermediate_zones[intermediate_zones_index]);
        }
    }

out:
    fclose(the_file);
}

int __real_main(int, char **);

int __wrap_main(int argc, char **argv) {

    int ret;
    int i;
    char *command;

    setup_memory_access_rules();

    asl_randomization = (unsigned long)elf_parse;
    AUDIT
    printf("runtime address of elf_parse is %p\n", elf_parse);
    asl_randomization =
        (unsigned long)((long)asl_randomization -
                        (long)find_elf_parse_compile_time_address(
                            disassembly_file));
    AUDIT
    printf("asl randomization is set to the value %p\n",
           (void *)asl_randomization);
    fflush(stdout);

    i = 0;
    functions[i] = strtok(target_functions, ",");
    while (functions[i]) {
        i++;
        functions[i] = strtok(NULL, ",");
    }

    ret = elf_parse(functions, disassembly_file);

    AUDIT
    printf("found %d instructions to instrument\n", ret);
    fflush(stdout);

    find_intermediate_zones(disassembly_file);

    build_intermediate_representation();

    build_patches();

#ifdef APPLY_PATCHES
    apply_patches();
#endif
    AUDIT {
        printf("__mvm: list of instructions instrumented\n");
        for (i = 0; i < ret; i++) {
            audit_block(instructions + i);
        }

        printf("patches applied - control goes to the actual program\n");
        fflush(stdout);
    }

    return __real_main(argc, argv);

} // this is the end of the MVM execution
