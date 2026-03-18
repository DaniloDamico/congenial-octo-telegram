#ifndef _MVM_
#define _MVM_

#define LANDING_PAD_SLOT "cli;nop;nop;nop;nop;"
#define LANDING_PAD_REPEAT_4 \
    LANDING_PAD_SLOT LANDING_PAD_SLOT LANDING_PAD_SLOT LANDING_PAD_SLOT
#define LANDING_PAD_REPEAT_16 \
    LANDING_PAD_REPEAT_4 LANDING_PAD_REPEAT_4 LANDING_PAD_REPEAT_4 LANDING_PAD_REPEAT_4
#define LANDING_PAD_REPEAT_32 \
    LANDING_PAD_REPEAT_16 LANDING_PAD_REPEAT_16
#define WIDE_LANDING_PAD \
    LANDING_PAD_REPEAT_32

#define INSTRUMENT_EXPR(expr) ({ INSTRUMENT; (expr); })

#define INSTRUMENT                                                                                                    \
    do {                                                                                                              \
        asm volatile("jmp 1f;" WIDE_LANDING_PAD "1:" ::);                                                           \
    } while (0)

#endif
