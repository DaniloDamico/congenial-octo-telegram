#ifndef M
#define M 1
#endif
#define CHUNKS_IN_LIST (8000)
#define REALLOCATION (CHUNKS_IN_LIST / 1000)
#define P (CHUNKS_IN_LIST >> 5)
#define TA (LOOKAHEAD * 1)
