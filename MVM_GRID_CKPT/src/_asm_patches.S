.global the_patch_assembly;
.global ckpt_assembly;
.global dummy_ckpt;

.section .text;

// ASM_PREAMBLE
the_patch_assembly:
	lea -0x1000(%rsp),%rsp //this is for the stack red zone
	pushf
        pushq %rdi
        pushq %rsi
        pushq %rdx
        pushq %rcx
        pushq %rax
        pushq %r8
        pushq %r9
        pushq %r10
        pushq %r11
        pushq %rbx
        pushq %rbp
        pushq %r12
        pushq %r13
        pushq %r14
        pushq %r15
	movq %rsp,%rsi
        movabs $0x00000000ff,%rdi
     	call the_patch 
       	popq %r15
        popq %r14
        popq %r13
        popq %r12
        popq %rbp
        popq %rbx
        popq %r11
        popq %r10
        popq %r9
        popq %r8
        popq %rax //index 11
        popq %rcx
        popq %rdx //index 13
        popq %rsi
        popq %rdi
	popf
	lea 0x1000(%rsp),%rsp

// CKPT ASM CODE
#if MOD != 8 && MOD != 16 && MOD != 32 && MOD != 64
#error "Valid MODs are 64, 128, 256, and 512."
#endif

#define LOG2_8 3
#define LOG2_16 4
#define LOG2_32 5
#define LOG2_64 6

// Helper macro to concatenate and evaluate
#define LOG2_EVAL(x) LOG2_##x
#define LOG2(x) LOG2_EVAL(x)

ckpt_assembly:
        mov %rax, %gs:0x00
        mov %rbx, %gs:0x08
        lahf
        mov %ah, %gs:0x18
#if MOD == 16
        movdqa %xmm1, %gs:0x20
#elif MOD == 32
        vmovdqa %ymm1, %gs:0x20
#elif MOD == 64
        vmovdqa64 %zmm1, %gs:0x40
#endif
        mov %rcx, %rax
        and $(~(ALLOCATOR_AREA_SIZE - 1)), %rcx
        test $(MOD - 1), %al
        jz aligned
        and $(ALLOCATOR_AREA_SIZE - MOD),%rax
#if MOD == 16
        movdqa (%rcx, %rax,), %xmm1
#elif MOD == 32
        vmovdqa (%rcx, %rax,), %ymm1
#elif MOD == 64
        vmovdqa64 (%rcx, %rax,), %zmm1
#endif
#if ALLOCATOR_AREA_SIZE <= 0xFFFFFFFF
        shr $(LOG2(MOD)), %eax
#else
        shr $(LOG2(MOD)), %rax
#endif
        mov %rax, %rbx
        and $15, %rbx
#if ALLOCATOR_AREA_SIZE <= 0xFFFFFFFF
        shr $4, %eax
#else
        shr $4, %rax
#endif
        bts %bx, (2 *ALLOCATOR_AREA_SIZE)(%rcx, %rax, 2)
        jc next
#if ALLOCATOR_AREA_SIZE <= 0xFFFFFFFF
        shl $4, %eax
        add %ebx, %eax
        shl $(LOG2(MOD)), %eax
#else
        shl $4, %rax
        add %rbx, %rax
        shl $(LOG2(MOD)), %rax
#endif
#if MOD == 8
        mov (%rcx, %rax,), %rbx
        mov %rbx, ALLOCATOR_AREA_SIZE(%rcx, %rax,)
#elif MOD == 16
        movdqa %xmm1, ALLOCATOR_AREA_SIZE(%rcx, %rax,)
#elif MOD == 32
        vmovdqa %ymm1, ALLOCATOR_AREA_SIZE(%rcx, %rax,)
#elif MOD == 64
        vmovdqa64 %zmm1, ALLOCATOR_AREA_SIZE(%rcx, %rax,)
#endif
        jmp check_last
next:
#if ALLOCATOR_AREA_SIZE <= 0xFFFFFFFF
        shl $4, %eax
        add %ebx, %eax
        shl $(LOG2(MOD)), %eax
#else
        shl $4, %rax
        add %rbx, %rax
        shl $(LOG2(MOD)), %rax
#endif
check_last:
#if ALLOCATOR_AREA_SIZE <= 0xFFFFFFFF
        cmp $(ALLOCATOR_AREA_SIZE - 8), %eax
        je end_checkpoint
        add $MOD, %eax
#else
        cmp $(ALLOCATOR_AREA_SIZE - ), %rax
        je end_checkpoint
        add $MOD, %rax
#endif
aligned:
        and $(ALLOCATOR_AREA_SIZE - 1), %rax
#if MOD == 16
        movdqa (%rcx, %rax,), %xmm1
#elif MOD == 32
        vmovdqa (%rcx, %rax,), %ymm1
#elif MOD == 64
        vmovdqa64 (%rcx, %rax,), %zmm1
#endif
#if ALLOCATOR_AREA_SIZE <= 0xFFFFFFFF
        shr $(LOG2(MOD)), %eax
#else
        shr $(LOG2(MOD)), %rax
#endif
        mov %rax, %rbx
        and $15, %rbx
#if ALLOCATOR_AREA_SIZE <= 0xFFFFFFFF
        shr $4, %eax
#else
        shr $4, %rax
#endif
        bts %bx, (2 *ALLOCATOR_AREA_SIZE)(%rcx, %rax, 2)
        jc end_checkpoint
#if ALLOCATOR_AREA_SIZE <= 0xFFFFFFFF
        shl $4, %eax
        add %ebx, %eax
        shl $(LOG2(MOD)), %eax
#else
        shl $4, %rax
        add %rbx, %rax
        shl $(LOG2(MOD)), %rax
#endif
#if MOD == 8
        mov (%rcx, %rax,), %rbx
        mov %rbx, ALLOCATOR_AREA_SIZE(%rcx, %rax,)
#elif MOD == 16
        movdqa %xmm1, ALLOCATOR_AREA_SIZE(%rcx, %rax,)
#elif MOD == 32
        vmovdqa %ymm1, ALLOCATOR_AREA_SIZE(%rcx, %rax,)
#elif MOD == 64
        vmovdqa64 %zmm1, ALLOCATOR_AREA_SIZE(%rcx, %rax,)
#endif
end_checkpoint:
#if MOD == 16
        movdqa %gs:0x20, %xmm1
#elif MOD == 32
        vmovdqa %gs:0x20, %ymm1
#elif MOD == 64
        vmovdqa64 %gs:0x40, %zmm1
#endif
        mov %gs:0x18, %ah
        sahf
        mov %gs:0x00, %rax
        mov %gs:0x08, %rbx
        mov %gs:0x10, %rcx

dummy_ckpt:
        nop