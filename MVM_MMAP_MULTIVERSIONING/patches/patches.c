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

/* MVMM usa sempre due slot per pagina e alterna tra i due durante il COW. */
enum
{
    MVMM_SLOT_0 = 0u,
    MVMM_SLOT_1 = 1u,
    MVMM_NUM_SLOTS = 2u
};

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

/* Stato MVMM associato a una singola pagina della regione tracciata. */
typedef struct
{
    uint64_t last_ts_seen;            // Ultimo epoch in cui e' stato aperto un nuovo slot.
    uint64_t last_touched_ts;         // Ultimo epoch in cui la pagina e' stata toccata da una store.
    uint32_t cur_slot;                // Slot attualmente pubblicato per load/store.
    uint64_t slot_ts[MVMM_NUM_SLOTS]; // Epoch associato a ciascuno slot.
    void *slots[MVMM_NUM_SLOTS];      // Buffer fisici dei due slot.
} mvmm_page_state;

/* Stato MVMM associato a una singola regione ottenuta via mmap. */
typedef struct
{
    uintptr_t base;         // Base della regione mmap tracciata.
    size_t len;             // Lunghezza in byte della regione.
    size_t npages;          // Numero di pagine logiche nella regione.
    mvmm_page_state *pages; // Stato MVMM per ogni pagina della regione.
#ifdef MMAP_MV_STORE_GRID
    size_t grid_cells_per_page;
    size_t grid_bitmap_bytes;
    uint8_t *grid_curr;
#endif

    // Pagine toccate nell'epoch corrente, usate per il rollback rapido.
    size_t *touched_curr;
    size_t touched_curr_count;
    uint64_t touched_curr_ts;

#ifndef MMAP_MV_STORE_GRID
    // Pagine toccate nell'epoch precedente, usate solo nella variante non-grid.
    size_t *touched_prev;
    size_t touched_prev_count;
    uint64_t touched_prev_ts;
#endif
} mvmm_region;

/* Epoch globale del motore di checkpoint/rollback. Parte da 0 all'init. */
static volatile uint64_t g_epoch_round = 0;
static volatile uint64_t g_ckpt_counter = 0;

#ifdef MMAP_MV_STORE
void *__real_memcpy(void *dest, const void *src, size_t n);
#define MVMM_MEMCPY __real_memcpy
#else
#define MVMM_MEMCPY memcpy
#endif

/* Nodo della lista globale delle regioni mmap tracciate da MVMM. */
typedef struct mvmm_region_node
{
    mvmm_region r;
    struct mvmm_region_node *next;
} mvmm_region_node;

/* Lista globale delle regioni registrate dal wrapper di mmap. */
static mvmm_region_node *g_regions_head = NULL;

/* Cache thread-local dell'ultima regione usata per accelerare i lookup. */
static __thread mvmm_region *g_cached_region = NULL;

/* Base delle regioni dell'allocatore PARSIR, una per object. */
extern void *base[OBJECTS];

static inline uintptr_t mvmm_translate_ea(mvmm_region *r, uintptr_t ea, int is_store);

void the_patch(unsigned long mem, unsigned long regs) __attribute__((used));
void set_ckpt(int object) __attribute__((used));
void restore_object(int object) __attribute__((used));
void *mmap_mv_memcpy(void *dest, const void *src, size_t n) __attribute__((used));
#ifdef MMAP_MV_STORE
void *__wrap_memcpy(void *dest, const void *src, size_t n) __attribute__((used));
#endif

/* Alloca una pagina allineata a MVMM_PAGE_SIZE per gli slot dinamici. */
static inline void *mvmm_alloc_page(void)
{

    void *p = NULL;
    if (posix_memalign(&p, MVMM_PAGE_SIZE, MVMM_PAGE_SIZE) != 0)
    {
        return NULL;
    }
    return p;
}

/* Restituisce vero se l'indirizzo appartiene alla regione indicata. */
static inline int mvmm_region_contains(const mvmm_region *r, uintptr_t address)
{
    return (address >= r->base && address < r->base + r->len);
}

/* Cerca linearmente la regione che contiene l'indirizzo richiesto. */
static inline mvmm_region *mvmm_find_region(uintptr_t address)
{
    for (mvmm_region_node *n = g_regions_head; n != NULL; n = n->next)
    {
        if (mvmm_region_contains(&n->r, address))
        {
            return &n->r;
        }
    }
    return NULL;
}

/* Cerca una regione usando prima la cache thread-local e poi la lista globale. */
static inline mvmm_region *mvmm_find_region_cached(uintptr_t address)
{
    mvmm_region *cached = g_cached_region;
    if (cached && mvmm_region_contains(cached, address))
    {
        return cached;
    }

    cached = mvmm_find_region(address);
    g_cached_region = cached;
    return cached;
}

/* Risolve la regione MVMM associata all'object PARSIR indicato. */
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
/* Restituisce il bitmap delle celle sporche per la pagina indicata. */
static inline uint8_t *mvmm_store_grid_bitmap(mvmm_region *r, size_t page_idx)
{
    return r->grid_curr + page_idx * r->grid_bitmap_bytes;
}

/* Restituisce la dimensione reale della cella di griglia dentro la pagina. */
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

/* Verifica se la cella di griglia e' gia' stata snapshotata nell'epoch corrente. */
static inline int mvmm_store_grid_cell_is_set(const uint8_t *bitmap, size_t cell_idx)
{
    return (bitmap[cell_idx >> 3] >> (cell_idx & 7)) & 1u;
}

/* Marca una cella di griglia come gia' snapshotata. */
static inline void mvmm_store_grid_cell_set(uint8_t *bitmap, size_t cell_idx)
{
    bitmap[cell_idx >> 3] |= (uint8_t)(1u << (cell_idx & 7));
}
#endif

/* Ruota le touched-list quando cambia l'epoch osservato dalla variante store. */
static inline void mvmm_store_rotate_touched(mvmm_region *r, uint64_t ts)
{
    if (r->touched_curr_ts == ts)
        return;

#ifndef MMAP_MV_STORE_GRID
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

/* Registra una pagina come toccata una sola volta per epoch. */
static inline void mvmm_store_note_page_touch(mvmm_region *r,
                                              mvmm_page_state *ps,
                                              size_t page_idx,
                                              uint64_t ts)
{
    mvmm_store_rotate_touched(r, ts);

    if (ps->last_touched_ts == ts)
        return;

    ps->last_touched_ts = ts;
    r->touched_curr[r->touched_curr_count++] = page_idx;
}

/* Garantisce che lo slot di snapshot esista ed e' pronto per l'epoch corrente. */
static inline int mvmm_store_prepare_snapshot_slot(mvmm_region *r,
                                                   mvmm_page_state *ps,
                                                   size_t page_idx,
                                                   uint64_t ts)
{
    mvmm_store_note_page_touch(r, ps, page_idx, ts);

    if (ps->slots[MVMM_SLOT_1] == NULL)
    {
        ps->slots[MVMM_SLOT_1] = mvmm_alloc_page();
        if (!ps->slots[MVMM_SLOT_1])
            return 0;
    }

#ifdef MMAP_MV_STORE_GRID
    if (ps->slot_ts[MVMM_SLOT_1] != ts)
    {
        memset(mvmm_store_grid_bitmap(r, page_idx), 0, r->grid_bitmap_bytes);
        ps->slot_ts[MVMM_SLOT_1] = ts;
    }
#endif

    return 1;
}

#ifdef MMAP_MV_STORE_GRID
/* Snapshotta solo le celle toccate dell'intervallo richiesto nella pagina. */
static inline int mvmm_store_snapshot_page_cells(mvmm_region *r,
                                                 size_t page_idx,
                                                 size_t page_off_start,
                                                 size_t page_off_end,
                                                 uint64_t ts)
{
    mvmm_page_state *ps = &r->pages[page_idx];
    uint8_t *bitmap;
    size_t first_cell;
    size_t last_cell;

    if (page_off_start >= page_off_end)
        return 1;

    if (!mvmm_store_prepare_snapshot_slot(r, ps, page_idx, ts))
        return 0;

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

        MVMM_MEMCPY((uint8_t *)ps->slots[MVMM_SLOT_1] + cell_off,
                    (uint8_t *)ps->slots[MVMM_SLOT_0] + cell_off,
                    cell_len);
        mvmm_store_grid_cell_set(bitmap, cell_idx);
    }

    ps->last_ts_seen = ts;
    return 1;
}
#endif

/* Snapshotta l'intera pagina oppure delega alla variante grid se attiva. */
static inline int mvmm_store_snapshot_page(mvmm_region *r, size_t page_idx, uint64_t ts)
{
#ifdef MMAP_MV_STORE_GRID
    return mvmm_store_snapshot_page_cells(r, page_idx, 0, MVMM_PAGE_SIZE, ts);
#else
    mvmm_page_state *ps = &r->pages[page_idx];

    if (!mvmm_store_prepare_snapshot_slot(r, ps, page_idx, ts))
        return 0;

    if (ps->slot_ts[MVMM_SLOT_1] == ts)
        return 1;

    MVMM_MEMCPY(ps->slots[MVMM_SLOT_1], ps->slots[MVMM_SLOT_0], MVMM_PAGE_SIZE);
    ps->slot_ts[MVMM_SLOT_1] = ts;
    ps->last_ts_seen = ts;
    return 1;
#endif
}

/* Snapshotta tutte le pagine coperte da una store o da una memcpy su intervallo. */
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

/* Restituisce il buffer fisico pubblicato per la pagina logica richiesta. */
static inline void *mvmm_get_current_page_ptr(const mvmm_region *r, size_t page_idx)
{
    mvmm_page_state *ps = &r->pages[page_idx];
    uint32_t slot = ps->cur_slot;
    return ps->slots[slot];
}

/* Calcola l'effective address della memory instruction a partire dai registri salvati. */
static inline uintptr_t mvmm_compute_effective_address(instruction_record *ins, unsigned long regs)
{
    if (ins->effective_operand_address != 0x0)
    {
        return (uintptr_t)ins->effective_operand_address;
    }

    target_address *t = &ins->target;

    /* I registri nello snapshot sono memorizzati a stride di 8 byte. */
    unsigned long base_reg_value = 0;
    unsigned long index_reg_value = 0;
    if (t->base_index)
        MVMM_MEMCPY(&base_reg_value, (void *)(regs + 8 * (t->base_index - 1)), 8);
    if (t->scale_index)
        MVMM_MEMCPY(&index_reg_value, (void *)(regs + 8 * (t->scale_index - 1)), 8);

    long disp = (long)t->displacement;
    long base = (long)base_reg_value;
    long idx = (long)index_reg_value;
    long sc = (long)t->scale;

    return (uintptr_t)(disp + base + idx * sc);
}

/* Riscrive il registro base per far puntare l'istruzione originale allo slot tradotto. */
static inline int mvmm_rewrite_base_reg_for_ea(instruction_record *ins,
                                               unsigned long regs,
                                               uintptr_t ea,
                                               uintptr_t ea_prime)
{
    if (ins->rip_relative == 'y')
        return 0;

    target_address *t = &ins->target;
    if (t->base_index == 0)
        return 0;

    if (t->scale_index != 0)
        return 0;

    /* Il delta rappresenta lo scostamento tra indirizzo originale e indirizzo tradotto. */
    intptr_t delta = (intptr_t)(ea_prime - ea);
    if (delta == 0)
        return 1;

    uint64_t base_val = 0;
    void *base_slot = (void *)(regs + 8 * (t->base_index - 1));
    MVMM_MEMCPY(&base_val, base_slot, 8);

    base_val = (uint64_t)((intptr_t)base_val + delta);
    MVMM_MEMCPY(base_slot, &base_val, 8);
    return 1;
}

/* Registra una nuova regione mmap nella lista globale del backend MVMM. */
static void mvmm_region_register(void *base, size_t len)
{
    if (base == MAP_FAILED || base == NULL || len == 0)
        return;

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
#ifndef MMAP_MV_STORE_GRID
    r->touched_prev = (size_t *)calloc(r->npages, sizeof(size_t));
#endif
#ifdef MMAP_MV_STORE_GRID
    if (!r->touched_curr)
#else
    if (!r->touched_curr || !r->touched_prev)
#endif
    {
        fprintf(stderr, "[mvmm] alloc touched-pages lists failed\n");
        free(r->touched_curr);
#ifndef MMAP_MV_STORE_GRID
        free(r->touched_prev);
#endif
#ifdef MMAP_MV_STORE_GRID
        free(r->grid_curr);
#endif
        free(r->pages);
        free(node);
        abort();
    }
    r->touched_curr_count = 0;
    r->touched_curr_ts = UINT64_MAX;
#ifndef MMAP_MV_STORE_GRID
    r->touched_prev_count = 0;
    r->touched_prev_ts = UINT64_MAX;
#endif

    /*
     * Inizializza ogni pagina.
     * La pagina reale e' lo snapshot iniziale, ma non la consideriamo
     * un checkpoint valido finche' non viene materializzata una prima
     * versione MVMM in uno degli slot dinamici.
     */
    for (size_t i = 0; i < r->npages; i++)
    {
        mvmm_page_state *ps = &r->pages[i];

        ps->last_ts_seen = UINT64_MAX;
        ps->last_touched_ts = UINT64_MAX;
        ps->cur_slot = MVMM_SLOT_0;

        ps->slots[MVMM_SLOT_0] = (void *)(r->base + i * MVMM_PAGE_SIZE);
        ps->slot_ts[MVMM_SLOT_0] = UINT64_MAX;

        ps->slots[MVMM_SLOT_1] = NULL;
        ps->slot_ts[MVMM_SLOT_1] = UINT64_MAX;
    }

    node->next = g_regions_head;
    g_regions_head = node;

}

void *__real_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);

/* Wrapper del linker per mmap: registra automaticamente ogni nuova regione tracciabile. */
void *__wrap_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    void *p = __real_mmap(addr, length, prot, flags, fd, offset);
    if (p != MAP_FAILED)
    {
        mvmm_region_register(p, length);
    }
    return p;
}

/* memcpy consapevole di MVMM: snapshotta o traduce gli indirizzi prima di copiare. */
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
/* Wrapper del linker per inoltrare memcpy standard alla variante MVMM-aware. */
void *__wrap_memcpy(void *dest, const void *src, size_t n)
{
    return mmap_mv_memcpy(dest, src, n);
}
#endif

/* Traduce un effective address verso il buffer MVMM corretto per la pagina corrente. */
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
    /* Nella variante base le store aprono una nuova versione al primo write dell'epoch. */
    if (is_store)
    {
        uint64_t ts = __atomic_load_n(&g_epoch_round, __ATOMIC_ACQUIRE);

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

        if (ps->last_touched_ts != ts)
        {
            ps->last_touched_ts = ts;
            r->touched_curr[r->touched_curr_count++] = page_idx;
        }

        /* Apre il secondo slot solo alla prima store osservata per quell'epoch. */
        if (ps->last_ts_seen != ts)
        {
            ps->last_ts_seen = ts;
            uint32_t cur = ps->cur_slot;
            uint32_t next = cur ^ 1u;

            void *dst = ps->slots[next];
            if (!dst)
            {
                dst = mvmm_alloc_page();
                if (!dst)
                    return ea;
                ps->slots[next] = dst;
            }

            void *src = ps->slots[cur];
            if (!src)
                return ea;

            MVMM_MEMCPY(dst, src, MVMM_PAGE_SIZE);

            ps->slot_ts[next] = ts;
            ps->cur_slot = next;
        }
    }

    uintptr_t off = ea - page_base;
    void *curp = mvmm_get_current_page_ptr(r, page_idx);
    if (!curp)
        return ea;
    return (uintptr_t)curp + off;
#endif
}

/* Verifica se un accesso oltrepassa il confine della pagina corrente. */
static inline int mvmm_is_cross_page(uintptr_t ea, size_t size)
{
    size_t off = (size_t)(ea & (MVMM_PAGE_SIZE - 1));
    return (off + size) > MVMM_PAGE_SIZE;
}

/* Entry point imposto da MVM: intercetta ogni accesso strumentato prima dell'istruzione originale. */
void the_patch(unsigned long mem, unsigned long regs)
{
    instruction_record *ins = (instruction_record *)mem;
    if (!ins)
        return;

#ifdef MMAP_MV_STORE
    if (ins->type != 's')
        return;
#else
    if (ins->type != 's' && ins->type != 'l')
        return;
#endif

    uintptr_t ea = mvmm_compute_effective_address(ins, regs);
    if (ea == 0)
        return;

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
        return;
    }

    int is_store = (ins->type == 's');
    uintptr_t ea_prime = mvmm_translate_ea(r, ea, is_store);

    if (!mvmm_rewrite_base_reg_for_ea(ins, regs, ea, ea_prime))
        return;
#endif
}

/* Buffer richiesto dall'interfaccia MVM ma non usato da questo backend. */
#define buffer user_defined_buffer
char buffer[1024];

/* Hook richiesto da MVM per patch custom testuali: qui e' volutamente vuoto. */
void user_defined(instruction_record *actual_instruction, patch *actual_patch)
{
    (void)actual_instruction;
    (void)actual_patch;
}

/* Ripristina una regione al checkpoint compatibile con il timestamp richiesto. */
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
        if (ps->slots[MVMM_SLOT_1] == NULL)
            continue;
        if (ps->slot_ts[MVMM_SLOT_1] == UINT64_MAX || ps->slot_ts[MVMM_SLOT_1] <= target_ts)
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

            MVMM_MEMCPY((uint8_t *)ps->slots[MVMM_SLOT_0] + cell_off,
                        (uint8_t *)ps->slots[MVMM_SLOT_1] + cell_off,
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
            if (ps->slots[MVMM_SLOT_1] == NULL)
                continue;

            if (ps->slot_ts[MVMM_SLOT_1] == UINT64_MAX || ps->slot_ts[MVMM_SLOT_1] <= target_ts)
                continue;

            MVMM_MEMCPY(ps->slots[MVMM_SLOT_0], ps->slots[MVMM_SLOT_1], MVMM_PAGE_SIZE);
            ps->last_ts_seen = UINT64_MAX;
            continue;
#else
            uint32_t best = UINT32_MAX;
            uint64_t best_ts = 0;
            uint64_t slot0_ts = ps->slot_ts[MVMM_SLOT_0];
            uint64_t slot1_ts = ps->slot_ts[MVMM_SLOT_1];

            if (slot0_ts != UINT64_MAX && slot0_ts <= target_ts)
            {
                best = MVMM_SLOT_0;
                best_ts = slot0_ts;
            }

            if (slot1_ts != UINT64_MAX && slot1_ts <= target_ts &&
                (best == UINT32_MAX || slot1_ts >= best_ts))
            {
                best = MVMM_SLOT_1;
                best_ts = slot1_ts;
            }

            if (best == UINT32_MAX)
                continue;

            ps->cur_slot = best;

            /* Costringe la prossima store ad aprire un nuovo COW sopra lo slot scelto. */
            ps->last_ts_seen = UINT64_MAX;

            if (slot0_ts != UINT64_MAX && slot0_ts > target_ts)
            {
                ps->slot_ts[MVMM_SLOT_0] = UINT64_MAX;
            }

            if (slot1_ts != UINT64_MAX && slot1_ts > target_ts)
            {
                ps->slot_ts[MVMM_SLOT_1] = UINT64_MAX;
            }
#endif
        }
    }
#endif
}

/* Wrapper minimale che protegge dal caso di regione nulla prima del rollback. */
static void mvmm_rollback_region(mvmm_region *r, uint64_t target_ts)
{
    if (!r)
        return;

    mvmm_region_rollback(r, target_ts);
}

/* Hook PARSIR: chiude il checkpoint dell'object e avanza l'epoch globale quando tutti hanno flushato. */
void set_ckpt(int object)
{
    set_allocator_ckpt(object);

    if (__sync_add_and_fetch(&g_ckpt_counter, 1) == OBJECTS)
    {
        __atomic_store_n(&g_ckpt_counter, 0, __ATOMIC_RELEASE);
        __sync_add_and_fetch(&g_epoch_round, 1);
    }
}

/* Hook PARSIR: ripristina l'object al checkpoint dell'epoch precedente. */
void restore_object(int object)
{
    mvmm_region *r = mvmm_find_region_for_object(object);
    uint64_t ts_now = __atomic_load_n(&g_epoch_round, __ATOMIC_ACQUIRE);
    uint64_t target_ts = (ts_now >= 1) ? (ts_now - 1) : 0;
    restore_allocator(object);
    mvmm_rollback_region(r, target_ts);
}
