#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/mman.h>

#include "elf_parse.h"
#include "memory.h"
#include "run.h"

#ifndef MVMM_MAX_VERSIONS
#define MVMM_MAX_VERSIONS 2
#endif

#ifdef MMAP_MV_STORE
#if MVMM_MAX_VERSIONS < 2
#error "MMAP_MV_STORE requires MVMM_MAX_VERSIONS >= 2"
#endif
#endif

#ifdef MMAP_MV_STORE_GRID
#ifndef MMAP_MV_STORE
#error "MMAP_MV_STORE_GRID requires MMAP_MV_STORE"
#endif
#endif

#ifndef MVMM_PAGE_SIZE
#define MVMM_PAGE_SIZE 4096
#endif

#ifndef MVMM_GRID_SIZE
#define MVMM_GRID_SIZE 64
#endif

#define MVMM_DEBUG 0

// se il flag é abilitato posso evitare di riallocare memoria ogni volta che faccio copy on write
// serve per il benchmark
#define SINGLE_THREAD 1

#if MVMM_DEBUG
#define MVMM_LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define MVMM_LOG(...) \
    do                \
    {                 \
    } while (0)
#endif

/*
 * Stato per pagina
 * Lo slot 0 all’inizio punta alla pagina reale della mmap
 */
typedef struct
{
    uint64_t last_ts_seen;               // ultimo ts per cui ho cambiato versione
    uint64_t last_touched_ts;            // ultimo ts in cui la pagina e' stata toccata in store
    uint32_t cur_slot;                   // slot corrente per load/store
    uint64_t slot_ts[MVMM_MAX_VERSIONS]; // timestamp di ogni slot
    void *slots[MVMM_MAX_VERSIONS];      // ptr pagina per ogni slot
} mvmm_page_state;

/*
 * Stato per regione (mmap)
 */
typedef struct
{
    uintptr_t base;         // base della regione mmap
    size_t len;             // lunghezza della regione
    size_t npages;          // numero di pagine
    mvmm_page_state *pages; // array di stati per pagina
#ifdef MMAP_MV_STORE_GRID
    size_t grid_cells_per_page;
    size_t grid_bitmap_bytes;
    uint8_t *grid_curr;
#endif

    // Liste pagine toccate negli ultimi due epoch (per rollback rapido).
    size_t *touched_curr;
    size_t touched_curr_count;
    uint64_t touched_curr_ts;

    size_t *touched_prev;
    size_t touched_prev_count;
    uint64_t touched_prev_ts;
} mvmm_region;

// Round corrente di checkpoint/rollback. Parte da 0 per l'INIT snapshot.
static volatile uint64_t g_epoch_round = 0;
static volatile uint64_t g_ckpt_counter = 0;

#ifdef MMAP_MV_STORE
void *__real_memcpy(void *dest, const void *src, size_t n);
#define MVMM_MEMCPY __real_memcpy
#else
#define MVMM_MEMCPY memcpy
#endif

/*
 * linked list delle regioni mmappate da tracciare.
 */
typedef struct mvmm_region_node
{
    mvmm_region r;
    struct mvmm_region_node *next;
} mvmm_region_node;

// Lista regioni registrate (una per mmap)
static mvmm_region_node *g_regions_head = NULL;

// Hot cache per-thread dell'ultima regione trovata.
static __thread mvmm_region *g_cached_region = NULL;

// Allocatore PARSIR: una regione mmap per object.
extern void *base[OBJECTS];

static inline uintptr_t mvmm_translate_ea(mvmm_region *r, uintptr_t ea, int is_store);

void the_patch(unsigned long mem, unsigned long regs) __attribute__((used));
void set_ckpt(int object) __attribute__((used));
void restore_object(int object) __attribute__((used));
void *mmap_mv_memcpy(void *dest, const void *src, size_t n) __attribute__((used));
#ifdef MMAP_MV_STORE
void *__wrap_memcpy(void *dest, const void *src, size_t n) __attribute__((used));
#endif

/*
 * Alloca una pagina allineata a g_page_size
 *
 * posix_memalign per garantire l’allineamento, al contrario di malloc
 */
static inline void *mvmm_alloc_page(void)
{

    void *p = NULL;
    if (posix_memalign(&p, MVMM_PAGE_SIZE, MVMM_PAGE_SIZE) != 0)
    {
        return NULL;
    }
    return p;
}

/* Ritorna 1 se l'indirizzo a appartiene alla regione r */
static inline int mvmm_region_contains(const mvmm_region *r, uintptr_t a)
{
    return (a >= r->base && a < r->base + r->len);
}

/*
 * Trova la regione che contiene l’indirizzo effettivo
 */
static inline mvmm_region *mvmm_find_region(uintptr_t ea)
{
    for (mvmm_region_node *n = g_regions_head; n != NULL; n = n->next)
    {
        if (mvmm_region_contains(&n->r, ea))
        {
            return &n->r;
        }
    }
    return NULL;
}

/*
 * Fast-path: prova prima con la regione usata dall'ultimo accesso del thread.
 */
static inline mvmm_region *mvmm_find_region_cached(uintptr_t ea)
{
    mvmm_region *cached = g_cached_region;
    if (cached && mvmm_region_contains(cached, ea))
    {
        return cached;
    }

    cached = mvmm_find_region(ea);
    g_cached_region = cached;
    return cached;
}

static inline mvmm_region *mvmm_find_region_for_object(int object)
{
    if (object < 0 || object >= OBJECTS)
        return NULL;

    if (base[object] == NULL)
        return NULL;

    return mvmm_find_region((uintptr_t)base[object]);
}

#ifdef MMAP_MV_STORE
#ifdef MMAP_MV_STORE_GRID
static inline uint8_t *mvmm_store_grid_bitmap(mvmm_region *r, size_t page_idx)
{
    return r->grid_curr + page_idx * r->grid_bitmap_bytes;
}

static inline size_t mvmm_store_grid_cell_size(const mvmm_region *r, size_t cell_idx)
{
    size_t start = cell_idx * MVMM_GRID_SIZE;
    size_t len = MVMM_GRID_SIZE;

    if (start >= MVMM_PAGE_SIZE)
        return 0;

    if (start + len > MVMM_PAGE_SIZE)
        len = MVMM_PAGE_SIZE - start;

    return len;
}

static inline int mvmm_store_grid_cell_is_set(const uint8_t *bitmap, size_t cell_idx)
{
    return (bitmap[cell_idx >> 3] >> (cell_idx & 7)) & 1u;
}

static inline void mvmm_store_grid_cell_set(uint8_t *bitmap, size_t cell_idx)
{
    bitmap[cell_idx >> 3] |= (uint8_t)(1u << (cell_idx & 7));
}
#endif

static inline void mvmm_store_rotate_touched(mvmm_region *r, uint64_t ts)
{
    if (r->touched_curr_ts == ts)
        return;

#ifdef MMAP_MV_STORE_GRID
    r->touched_prev_count = 0;
    r->touched_prev_ts = UINT64_MAX;
#else
    r->touched_prev_count = r->touched_curr_count;
    r->touched_prev_ts = r->touched_curr_ts;
    if (r->touched_prev_count > 0)
    {
        MVMM_MEMCPY(r->touched_prev, r->touched_curr, r->touched_prev_count * sizeof(size_t));
    }
#endif
    r->touched_curr_count = 0;
    r->touched_curr_ts = ts;
}

#ifdef MMAP_MV_STORE_GRID
static inline int mvmm_store_snapshot_page_cells(mvmm_region *r,
                                                 size_t page_idx,
                                                 size_t page_off_start,
                                                 size_t page_off_end,
                                                 uint64_t ts)
{
    uint32_t snapshot_slot = 1u;
    mvmm_page_state *ps = &r->pages[page_idx];
    uint8_t *bitmap;
    size_t first_cell;
    size_t last_cell;

    if (page_off_start >= page_off_end)
        return 1;

    mvmm_store_rotate_touched(r, ts);

    if (ps->last_touched_ts != ts)
    {
        ps->last_touched_ts = ts;
        r->touched_curr[r->touched_curr_count++] = page_idx;
    }

    if (ps->slots[snapshot_slot] == NULL)
    {
        ps->slots[snapshot_slot] = mvmm_alloc_page();
        if (!ps->slots[snapshot_slot])
            return 0;
    }

    if (ps->slot_ts[snapshot_slot] != ts)
    {
        memset(mvmm_store_grid_bitmap(r, page_idx), 0, r->grid_bitmap_bytes);
        ps->slot_ts[snapshot_slot] = ts;
    }

    bitmap = mvmm_store_grid_bitmap(r, page_idx);
    first_cell = page_off_start / MVMM_GRID_SIZE;
    last_cell = (page_off_end - 1) / MVMM_GRID_SIZE;

    for (size_t cell_idx = first_cell; cell_idx <= last_cell; cell_idx++)
    {
        size_t cell_off;
        size_t cell_len;

        if (mvmm_store_grid_cell_is_set(bitmap, cell_idx))
            continue;

        cell_off = cell_idx * MVMM_GRID_SIZE;
        cell_len = mvmm_store_grid_cell_size(r, cell_idx);
        if (cell_len == 0)
            continue;

        MVMM_MEMCPY((uint8_t *)ps->slots[snapshot_slot] + cell_off,
                    (uint8_t *)ps->slots[0] + cell_off,
                    cell_len);
        mvmm_store_grid_cell_set(bitmap, cell_idx);
    }

    ps->last_ts_seen = ts;
    return 1;
}
#endif

static inline int mvmm_store_snapshot_page(mvmm_region *r, size_t page_idx, uint64_t ts)
{
#ifdef MMAP_MV_STORE_GRID
    return mvmm_store_snapshot_page_cells(r, page_idx, 0, MVMM_PAGE_SIZE, ts);
#else
    uint32_t snapshot_slot = 1u;
    mvmm_page_state *ps = &r->pages[page_idx];
    void *snapshot;

    mvmm_store_rotate_touched(r, ts);

    if (ps->last_touched_ts != ts)
    {
        ps->last_touched_ts = ts;
        r->touched_curr[r->touched_curr_count++] = page_idx;
    }

    if (ps->slot_ts[snapshot_slot] == ts)
        return 1;

    snapshot = ps->slots[snapshot_slot];
    if (!snapshot)
    {
        snapshot = mvmm_alloc_page();
        if (!snapshot)
            return 0;
        ps->slots[snapshot_slot] = snapshot;
    }

    MVMM_MEMCPY(snapshot, ps->slots[0], MVMM_PAGE_SIZE);
    ps->slot_ts[snapshot_slot] = ts;
    ps->last_ts_seen = ts;
    return 1;
#endif
}

static inline void mvmm_store_snapshot_range(uintptr_t dst, size_t n)
{
    uintptr_t cur = dst;
    uintptr_t end = dst + n;
    uint64_t ts;

    if (n == 0 || end <= cur)
        return;

    ts = __atomic_load_n(&g_epoch_round, __ATOMIC_ACQUIRE);

    while (cur < end)
    {
        uintptr_t page_base = cur & ~((uintptr_t)MVMM_PAGE_SIZE - 1);
        uintptr_t next = page_base + MVMM_PAGE_SIZE;
        mvmm_region *r = mvmm_find_region_cached(cur);

        if (next <= cur || next > end)
            next = end;

        if (r)
        {
            size_t page_idx = (size_t)((page_base - r->base) / MVMM_PAGE_SIZE);
            if (page_idx < r->npages)
            {
#ifdef MMAP_MV_STORE_GRID
                size_t page_off_start = (size_t)(cur - page_base);
                size_t page_off_end = (size_t)(next - page_base);
                if (!mvmm_store_snapshot_page_cells(r, page_idx, page_off_start, page_off_end, ts))
                    return;
#else
                if (!mvmm_store_snapshot_page(r, page_idx, ts))
                    return;
#endif
            }
        }

        cur = next;
    }
}
#endif

/*
 * Restituisce il puntatore alla pagina corrente per quella pagina logica
 * param r: la regione trovata da mvmm_find_region
 * param page_idx: indice della pagina
 */
static inline void *mvmm_cur_page_ptr(const mvmm_region *r, size_t page_idx)
{
    mvmm_page_state *ps = &r->pages[page_idx];
    uint32_t slot = ps->cur_slot;
    return ps->slots[slot];
}

/*
 * Calcola l’EA usando i metadati MVM e lo snapshot dei registri.
 * Se ins->effective_operand_address è già valorizzato, lo usa direttamente.
 */
static inline uintptr_t mvm_get_ea_u(instruction_record *ins, unsigned long regs)
{
    if (ins->effective_operand_address != 0x0)
    {
        return (uintptr_t)ins->effective_operand_address;
    }

    target_address *t = &ins->target;

    // 8* perché i registri sono a 8 byte di distanza l’uno dall’altro
    // puntatore a registri (regs) + 8*(indice-1) per trovare quello giusto
    unsigned long A = 0, B = 0;
    if (t->base_index)
        MVMM_MEMCPY(&A, (void *)(regs + 8 * (t->base_index - 1)), 8);
    if (t->scale_index)
        MVMM_MEMCPY(&B, (void *)(regs + 8 * (t->scale_index - 1)), 8);

    long disp = (long)t->displacement;
    long base = (long)A;
    long idx = (long)B;
    long sc = (long)t->scale;

    return (uintptr_t)(disp + base + idx * sc);
}

/*
 * Riscrive il registro base in modo che l’istruzione originale, quando riprende, acceda a ea_prime.
 *
 * Solo indirizzamenti del tipo:
 *   [base + displacement]
 * quindi:
 * - no RIP-relative (non posso modificare RIP)
 * - base_index deve esserci
 * - scale_index deve essere 0
 */
static inline int mvmm_rewrite_base_reg_for_ea(instruction_record *ins,
                                               unsigned long regs,
                                               uintptr_t ea,       // indirizzo originale
                                               uintptr_t ea_prime) // indirizzo dello slot corrente
{
    if (ins->rip_relative == 'y')
        return 0;

    target_address *t = &ins->target;
    if (t->base_index == 0)
        return 0;

    if (t->scale_index != 0)
        return 0;

    // il delta é di quanto spostare il registro base
    intptr_t delta = (intptr_t)(ea_prime - ea);
    if (delta == 0)
        return 1;

    uint64_t base_val = 0;
    void *base_slot = (void *)(regs + 8 * (t->base_index - 1)); // prendo il registro base di regs perché acceda a ea_prime
    MVMM_MEMCPY(&base_val, base_slot, 8);

    base_val = (uint64_t)((intptr_t)base_val + delta);
    MVMM_MEMCPY(base_slot, &base_val, 8); // faccio il side effect sul registro base
    return 1;
}

/*
 * Registra una nuova regione mmap nella lista globale.
 */
static void mvmm_region_register(void *base, size_t len)
{
    if (base == MAP_FAILED || base == NULL || len == 0)
        return;

    // Alloca il nodo
    mvmm_region_node *node = (mvmm_region_node *)calloc(1, sizeof(*node));
    if (!node)
    {
        fprintf(stderr, "[mvmm] alloc region node failed\n");
        abort();
    }

    mvmm_region *r = &node->r;
    r->base = (uintptr_t)base;
    r->len = len;
    r->npages = (len + MVMM_PAGE_SIZE - 1) / MVMM_PAGE_SIZE; // arrotonda per eccesso
#ifdef MMAP_MV_STORE_GRID
    r->grid_cells_per_page = (MVMM_PAGE_SIZE + MVMM_GRID_SIZE - 1) / MVMM_GRID_SIZE;
    r->grid_bitmap_bytes = (r->grid_cells_per_page + 7) >> 3;
#endif

    r->pages = (mvmm_page_state *)calloc(r->npages, sizeof(*r->pages));
    if (!r->pages)
    {
        fprintf(stderr, "[mvmm] alloc pages failed\n");
        free(node);
        abort();
    }
#ifdef MMAP_MV_STORE_GRID
    r->grid_curr = (uint8_t *)calloc(r->npages, r->grid_bitmap_bytes);
    if (!r->grid_curr)
    {
        fprintf(stderr, "[mvmm] alloc grid bitmaps failed\n");
        free(r->pages);
        free(node);
        abort();
    }
#endif

    r->touched_curr = (size_t *)calloc(r->npages, sizeof(size_t));
    r->touched_prev = (size_t *)calloc(r->npages, sizeof(size_t));
    if (!r->touched_curr || !r->touched_prev)
    {
        fprintf(stderr, "[mvmm] alloc touched-pages lists failed\n");
        free(r->touched_curr);
        free(r->touched_prev);
#ifdef MMAP_MV_STORE_GRID
        free(r->grid_curr);
#endif
        free(r->pages);
        free(node);
        abort();
    }
    r->touched_curr_count = 0;
    r->touched_prev_count = 0;
    r->touched_curr_ts = UINT64_MAX;
    r->touched_prev_ts = UINT64_MAX;

    /*
     * Inizializza ogni pagina.
     * La pagina reale e' lo snapshot iniziale, ma non la consideriamo
     * un checkpoint valido finche' non viene materializzata una prima
     * versione MVMM in uno degli slot dinamici.
     */
    for (size_t i = 0; i < r->npages; i++)
    {
        mvmm_page_state *ps = &r->pages[i];

        ps->last_ts_seen = UINT64_MAX; // timestamp impossibile
        ps->last_touched_ts = UINT64_MAX;
        ps->cur_slot = 0;

        ps->slots[0] = (void *)(r->base + i * MVMM_PAGE_SIZE);
        ps->slot_ts[0] = UINT64_MAX;

        for (uint32_t s = 1; s < MVMM_MAX_VERSIONS; s++)
        {
            ps->slots[s] = NULL;
            ps->slot_ts[s] = UINT64_MAX;
        }
    }

    // aggiungi nuova regione in testa alla lista
    node->next = g_regions_head;
    g_regions_head = node;

    MVMM_LOG("[mvmm] region registered base=%p len=%zu pages=%zu page_size=%d\n",
            base, len, r->npages, MVMM_PAGE_SIZE);
}

void *__real_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);

void *__wrap_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    void *p = __real_mmap(addr, length, prot, flags, fd, offset);
    if (p != MAP_FAILED)
    {
        mvmm_region_register(p, length);
    }
    return p;
}

void *mmap_mv_memcpy(void *dest, const void *src, size_t n)
{
#ifdef MMAP_MV_STORE
    mvmm_store_snapshot_range((uintptr_t)dest, n);
    return __real_memcpy(dest, src, n);
#else
    uintptr_t dst = (uintptr_t)dest;
    uintptr_t srcp = (uintptr_t)src;
    mvmm_region *dst_r = mvmm_find_region_cached(dst);
    mvmm_region *src_r = mvmm_find_region_cached(srcp);

    if (!dst_r && !src_r)
        return MVMM_MEMCPY(dest, src, n);

    for (size_t i = 0; i < n; i++)
    {
        uintptr_t cur_src = mvmm_translate_ea(src_r, srcp + i, 0);
        uintptr_t cur_dst = mvmm_translate_ea(dst_r, dst + i, 1);
        *(unsigned char *)cur_dst = *(const unsigned char *)cur_src;
    }

    return dest;
#endif
}

#ifdef MMAP_MV_STORE
void *__wrap_memcpy(void *dest, const void *src, size_t n)
{
    return mmap_mv_memcpy(dest, src, n);
}
#endif

/*
 * Traduce l'indirizzo effettivo ea verso la versione corrente della pagina.
 *
 */
static inline uintptr_t mvmm_translate_ea(mvmm_region *r,
                                          uintptr_t ea,
                                          int is_store)
{
    if (!r)
        return ea;

    uintptr_t page_base = ea & ~((uintptr_t)MVMM_PAGE_SIZE - 1);        // indirizzo base della pagina dell'ea originale
    size_t page_idx = (size_t)((page_base - r->base) / MVMM_PAGE_SIZE); // numero pagina
    if (page_idx >= r->npages)
        return ea;

    mvmm_page_state *ps = &r->pages[page_idx];

#ifdef MMAP_MV_STORE
    if (!is_store)
        return ea;

    {
        uint64_t ts = __atomic_load_n(&g_epoch_round, __ATOMIC_ACQUIRE);
        if (!mvmm_store_snapshot_page(r, page_idx, ts))
            return ea;
    }

    return ea;
#else
    // se é una scrittura aumenta il contatore e fai copy on write se comincia una nuova era
    if (is_store)
    {
        // Versione corrente guidata dall'epoch del motore.
        uint64_t ts = __atomic_load_n(&g_epoch_round, __ATOMIC_ACQUIRE);

        // Ruota i buffer touched quando cambia epoch.
        if (r->touched_curr_ts != ts)
        {
            r->touched_prev_count = r->touched_curr_count;
            r->touched_prev_ts = r->touched_curr_ts;
            if (r->touched_prev_count > 0)
            {
                MVMM_MEMCPY(r->touched_prev, r->touched_curr, r->touched_prev_count * sizeof(size_t));
            }
            r->touched_curr_count = 0;
            r->touched_curr_ts = ts;
        }

        // Registra una sola volta per pagina/epoch nella lista touched corrente.
        if (ps->last_touched_ts != ts)
        {
            ps->last_touched_ts = ts;
            r->touched_curr[r->touched_curr_count++] = page_idx;
        }

        // 1 sola copy on write per pagina per round del motore.
        if (ps->last_ts_seen != ts)
        {
            ps->last_ts_seen = ts;
            uint32_t cur = ps->cur_slot;
#if MVMM_MAX_VERSIONS == 2
            uint32_t next = cur ^ 1u;
#else
            uint32_t next = cur + 1u;
            if (next >= MVMM_MAX_VERSIONS)
                next = 0;
#endif

            void *dst = NULL;
#if SINGLE_THREAD
            dst = ps->slots[next];
            if (!dst)
            {
                dst = mvmm_alloc_page();
                if (!dst)
                    return ea;
                ps->slots[next] = dst;
            }
#else
            dst = mvmm_alloc_page();
            if (!dst)
                return ea;
            ps->slots[next] = dst;
#endif

            void *src = ps->slots[cur];
            if (!src)
                return ea;

            MVMM_MEMCPY(dst, src, MVMM_PAGE_SIZE);

            ps->slot_ts[next] = ts;
            ps->cur_slot = next;

#if MVMM_DEBUG
            MVMM_LOG("[mvmm] COW ts=%lu region=%p page=%zu slot=%u\n",
                     (unsigned long)ts, (void *)r->base, page_idx, next);
#endif
        }
    }

    // Traduci ea verso cur_slot mantenendo l’offset dentro pagina
    uintptr_t off = ea - page_base;
    void *curp = mvmm_cur_page_ptr(r, page_idx);
    if (!curp)
        return ea;
    return (uintptr_t)curp + off;
#endif
}

/**
 * Controlla se l'accesso attraversa il confine di pagina, dato un indirizzo effettivo e una dimensione.
 */
static inline int mvmm_is_cross_page(uintptr_t ea, size_t size)
{
    size_t off = (size_t)(ea & (MVMM_PAGE_SIZE - 1));
    return (off + size) > MVMM_PAGE_SIZE;
}

/*
 * entry point
 */
void the_patch(unsigned long mem, unsigned long regs)
{
    instruction_record *ins = (instruction_record *)mem;
    if (!ins)
        return;

#ifdef MMAP_MV_STORE
    if (ins->type != 's')
        return;
#else
    // solo load e store
    if (ins->type != 's' && ins->type != 'l')
        return;
#endif

    // calcola effective address
    uintptr_t ea = mvm_get_ea_u(ins, regs);
    if (ea == 0)
        return;

    // trova regione tra quelle tracciate
    mvmm_region *r = mvmm_find_region_cached(ea);
    if (!r)
        return;

    size_t sz = (size_t)ins->data_size;
#ifdef MMAP_MV_STORE
    if (sz == 0)
        sz = 1;
    mvmm_store_snapshot_range(ea, sz);
    return;
#else
    if (sz != 0 && mvmm_is_cross_page(ea, sz))
    {
        MVMM_LOG("[mvmm] UNSUPPORTED cross-page %c ea=%p size=%zu\n",
                 ins->type, (void *)ea, sz);
        return;
    }
    // calcola ea' (versione corrente)
    int is_store = (ins->type == 's');
    uintptr_t ea_prime = mvmm_translate_ea(r, ea, is_store);

    // scrivi ea' nel registro di base di regs
    if (!mvmm_rewrite_base_reg_for_ea(ins, regs, ea, ea_prime))
        return;
#endif
}

// non usato
#define buffer user_defined_buffer
char buffer[1024];

void user_defined(instruction_record *actual_instruction, patch *actual_patch)
{
    (void)actual_instruction;
    (void)actual_patch;
}

// rollback

/* Rollback della regione r al timestamp target_ts.
 * Dopo questa chiamata, le load leggono dalla versione scelta.
 * La prima store aprirà una nuova versione sopra quella.
 *
 * non libera nulla, non gestisce munmap
 */
static void mvmm_region_rollback(mvmm_region *r, uint64_t target_ts)
{
    if (!r)
        return;

#ifdef MMAP_MV_STORE_GRID
    if (r->touched_curr_ts == UINT64_MAX || r->touched_curr_ts <= target_ts)
        return;

    for (size_t k = 0; k < r->touched_curr_count; k++)
    {
        size_t page_idx = r->touched_curr[k];
        mvmm_page_state *ps;
        uint8_t *bitmap;

        if (page_idx >= r->npages)
            continue;

        ps = &r->pages[page_idx];
        if (ps->slots[1] == NULL)
            continue;
        if (ps->slot_ts[1] == UINT64_MAX || ps->slot_ts[1] <= target_ts)
            continue;

        bitmap = mvmm_store_grid_bitmap(r, page_idx);
        for (size_t cell_idx = 0; cell_idx < r->grid_cells_per_page; cell_idx++)
        {
            size_t cell_off;
            size_t cell_len;

            if (!mvmm_store_grid_cell_is_set(bitmap, cell_idx))
                continue;

            cell_off = cell_idx * MVMM_GRID_SIZE;
            cell_len = mvmm_store_grid_cell_size(r, cell_idx);
            if (cell_len == 0)
                continue;

            MVMM_MEMCPY((uint8_t *)ps->slots[0] + cell_off,
                        (uint8_t *)ps->slots[1] + cell_off,
                        cell_len);
        }

        ps->last_ts_seen = UINT64_MAX;
    }

    return;
#else
    size_t candidates[2];
    uint64_t candidate_ts[2];
    int n = 0;

    if (r->touched_curr_ts != UINT64_MAX && r->touched_curr_ts > target_ts)
    {
        candidates[n] = r->touched_curr_count;
        candidate_ts[n] = r->touched_curr_ts;
        n++;
    }
    if (r->touched_prev_ts != UINT64_MAX && r->touched_prev_ts > target_ts)
    {
        candidates[n] = r->touched_prev_count;
        candidate_ts[n] = r->touched_prev_ts;
        n++;
    }

    for (int li = 0; li < n; li++)
    {
        size_t *list = (candidate_ts[li] == r->touched_curr_ts) ? r->touched_curr : r->touched_prev;
        size_t count = candidates[li];
        for (size_t k = 0; k < count; k++)
        {
            size_t page_idx = list[k];
            if (page_idx >= r->npages)
                continue;
            mvmm_page_state *ps = &r->pages[page_idx];

#ifdef MMAP_MV_STORE
            if (ps->slots[1] == NULL)
                continue;

            if (ps->slot_ts[1] == UINT64_MAX || ps->slot_ts[1] <= target_ts)
                continue;

            MVMM_MEMCPY(ps->slots[0], ps->slots[1], MVMM_PAGE_SIZE);
            ps->last_ts_seen = UINT64_MAX;
            continue;
#else
            uint32_t best = UINT32_MAX;
            uint64_t best_ts = 0;

            for (uint32_t s = 0; s < MVMM_MAX_VERSIONS; s++)
            {
                uint64_t ts = ps->slot_ts[s];
                if (ts == UINT64_MAX || ts > target_ts)
                    continue;

                if (best == UINT32_MAX || ts >= best_ts)
                {
                    best = s;
                    best_ts = ts;
                }
            }

            if (best == UINT32_MAX)
                continue;

            // Pubblica la scelta
            ps->cur_slot = best;

            // forza il prossimo store a fare copy on write
            ps->last_ts_seen = UINT64_MAX;

            // Invalida tutte le versioni future.
            for (uint32_t s = 0; s < MVMM_MAX_VERSIONS; s++)
            {
                uint64_t ts = ps->slot_ts[s];
                if (ts != UINT64_MAX && ts > target_ts)
                {
                    ps->slot_ts[s] = UINT64_MAX;
#if !SINGLE_THREAD
                    ps->slots[s] = NULL;
#endif
                }
            }

            MVMM_LOG("[mvmm] rollback page=%zu choose slot=%u best_ts=%lu\n",
                     page_idx, best, (unsigned long)best_ts);
#endif
        }
    }
#endif
}

static void mvmm_dump_state_locked(void)
{
    fprintf(stderr, "[mvmm] ===== DUMP STATE BEGIN =====\n");

    size_t rix = 0;
    for (mvmm_region_node *n = g_regions_head; n; n = n->next, rix++)
    {
        mvmm_region *r = &n->r;
        fprintf(stderr, "[mvmm] region[%zu] base=%p len=%zu npages=%zu\n",
                rix, (void *)r->base, r->len, r->npages);

        /* Per non stampare troppo, se vuoi limita il numero di pagine */
        for (size_t page_idx = 0; page_idx < r->npages; page_idx++)
        {
            mvmm_page_state *ps = &r->pages[page_idx];

            uint32_t cur = ps->cur_slot;
            uint64_t last = ps->last_ts_seen;

            fprintf(stderr, "  page[%zu]: cur_slot=%u last_ts_seen=%s\n",
                    page_idx, cur,
                    (last == UINT64_MAX) ? "UINT64_MAX" : "set");

            for (uint32_t s = 0; s < MVMM_MAX_VERSIONS; s++)
            {
                uint64_t ts = ps->slot_ts[s];
                void *p = ps->slots[s];

                if (ts == UINT64_MAX && p == NULL)
                {
                    fprintf(stderr, "    slot[%u]: EMPTY\n", s);
                }
                else
                {
                    fprintf(stderr, "    slot[%u]: ts=%s%lu ptr=%p%s\n",
                            s,
                            (ts == UINT64_MAX) ? "UINT64_MAX(" : "",
                            (unsigned long)ts,
                            p,
                            (ts == UINT64_MAX) ? ")" : "");
                }
            }
        }
    }

    fprintf(stderr, "[mvmm] ===== DUMP STATE END =====\n");
}

static void mvmm_rollback_region(mvmm_region *r, uint64_t target_ts)
{
    if (!r)
        return;

    /* dump prima del rollback */
#if MVMM_DEBUG
    fprintf(stderr, "[mvmm] rollback_region target_ts=%lu (BEFORE)\n",
            (unsigned long)target_ts);
    mvmm_dump_state_locked();
#endif

    mvmm_region_rollback(r, target_ts);

    /* dump dopo il rollback */
#if MVMM_DEBUG
    fprintf(stderr, "[mvmm] rollback_region target_ts=%lu (AFTER)\n",
            (unsigned long)target_ts);
    mvmm_dump_state_locked();
#endif
}

/*
 * Hooks required by PARSIR speculation path.
 * Il checkpoint e' globale: finche' non finiscono tutti gli object non
 * possiamo distruggere il round precedente. Per questo set_ckpt conta solo
 * gli object flushati e l'avanzamento del round e' globale.
 */
void set_ckpt(int object)
{
    set_allocator_ckpt(object);

    /*
     * Il flush checkpointa gli object in parallelo.
     * L'ultimo checkpoint completato chiude il round corrente e apre il successivo.
     */
    if (__sync_add_and_fetch(&g_ckpt_counter, 1) == OBJECTS)
    {
        __atomic_store_n(&g_ckpt_counter, 0, __ATOMIC_RELEASE);
        __sync_add_and_fetch(&g_epoch_round, 1);
    }
}

void restore_object(int object)
{
    mvmm_region *r = mvmm_find_region_for_object(object);
    uint64_t ts_now = __atomic_load_n(&g_epoch_round, __ATOMIC_ACQUIRE);
    // Allineato a grid_ckpt: ripristina il checkpoint dell'epoch precedente.
    uint64_t target_ts = (ts_now >= 1) ? (ts_now - 1) : 0;
    restore_allocator(object);
    mvmm_rollback_region(r, target_ts);
}
