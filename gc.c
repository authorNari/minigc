#ifdef DO_DEBUG
#define DEBUG(exp) (exp)
#else
#define DEBUG(exp)
#endif

#ifndef DO_DEBUG
#define NDEBUG
#endif

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <setjmp.h>
#include "gc.h"

/* ========================================================================== */
/*  mini_gc_malloc                                                            */
/* ========================================================================== */

typedef struct header {
    size_t flags;
    size_t size;
    struct header *next_free;
} Header;

typedef struct gc_heap {
    Header *slot;
    size_t size;
} GC_Heap;

#define TINY_HEAP_SIZE 0x4000
#define PTRSIZE ((size_t) sizeof(void *))
#define HEADER_SIZE ((size_t) sizeof(Header))
#define HEAP_LIMIT 10000
#define ALIGN(x,a) (((x) + (a - 1)) & ~(a - 1))
#define NEXT_HEADER(x) ((Header *)((size_t)(x+1) + x->size))

/* flags */
#define FL_ALLOC 0x1
#define FL_MARK 0x2
#define FL_SET(x, f) (((Header *)x)->flags |= f)
#define FL_UNSET(x, f) (((Header *)x)->flags &= ~(f))
#define FL_TEST(x, f) (((Header *)x)->flags & f)

static Header *free_list = NULL;
static GC_Heap gc_heaps[HEAP_LIMIT];
static size_t gc_heaps_used = 0;


static Header *
add_heap(size_t req_size)
{
    char *p;
    Header *align_p;

    if (gc_heaps_used >= HEAP_LIMIT) {
        fputs("OutOfMemory Error", stderr);
        abort();
    }

    if (req_size < TINY_HEAP_SIZE)
        req_size = TINY_HEAP_SIZE;

    if((p = sbrk(req_size + PTRSIZE + HEADER_SIZE)) == (char *)-1)
        return NULL;

    /* address alignment */
    align_p = gc_heaps[gc_heaps_used].slot = (Header *)ALIGN((size_t)p, PTRSIZE);
    req_size = gc_heaps[gc_heaps_used].size = req_size;
    align_p->size = req_size;
    align_p->next_free = align_p;
    gc_heaps_used++;

    return align_p;
}

static Header *
grow(size_t req_size)
{
    Header *cp, *up;

    if (!(cp = add_heap(req_size)))
        return NULL;

    up = (Header *) cp;
    mini_gc_free((void *)(up+1));
    return free_list;
}

void *
mini_gc_malloc(size_t req_size)
{
    Header *p, *prevp;
    size_t do_gc = 0;

    req_size = ALIGN(req_size, PTRSIZE);

    if (req_size <= 0) {
        return NULL;
    }
    if ((prevp = free_list) == NULL) {
        if (!(p = add_heap(TINY_HEAP_SIZE))) {
            return NULL;
        }
        prevp = free_list = p;
    }
    for (p = prevp->next_free; ; prevp = p, p = p->next_free) {
        if (p->size >= req_size) {
            if (p->size == req_size)
                /* just fit */
                prevp->next_free = p->next_free;
            else {
                /* too big */
                p->size -= (req_size + HEADER_SIZE);
                p = NEXT_HEADER(p);
                p->size = req_size;
            }
            free_list = prevp;
            FL_SET(p, FL_ALLOC);
            return (void *)(p+1);
        }
        if (p == free_list) {
            if (!do_gc) {
                garbage_collect();
                do_gc = 1;
            }
            else if ((p = grow(req_size)) == NULL)
                return NULL;
        }
    }
}

void
mini_gc_free(void *ptr)
{
    Header *target, *hit;

    target = (Header *)ptr - 1;

    /* search join point of target to free_list */
    for (hit = free_list; !(target > hit && target < hit->next_free); hit = hit->next_free)
        /* heap end? And hit(search)? */
        if (hit >= hit->next_free &&
            (target > hit || target < hit->next_free))
            break;

    if (NEXT_HEADER(target) == hit->next_free) {
        /* merge */
        target->size += (hit->next_free->size + HEADER_SIZE);
        target->next_free = hit->next_free->next_free;
    }
    else {
        /* join next free block */
        target->next_free = hit->next_free;
    }
    if (NEXT_HEADER(hit) == target) {
        /* merge */
        hit->size += (target->size + HEADER_SIZE);
        hit->next_free = target->next_free;
    }
    else {
        /* join before free block */
        hit->next_free = target;
    }
    free_list = hit;
    target->flags = 0;
}




/* ========================================================================== */
/*  mini_gc                                                                   */
/* ========================================================================== */

struct root_range {
    char * start;
    char * end;
};

#define IS_MARKED(x) (FL_TEST(x, FL_ALLOC) && FL_TEST(x, FL_MARK))
#define ROOT_RANGES_LIMIT 1000

static struct root_range root_ranges[ROOT_RANGES_LIMIT];
static size_t root_ranges_used = 0;
static char * stack_start = NULL;
static char * stack_end = NULL;
static GC_Heap *hit_chach = NULL;

static GC_Heap *
is_pointer_to_heap(void *ptr)
{
    size_t i;

    if (hit_chach &&
        ((void *)hit_chach->slot) <= ptr &&
        (size_t)ptr < (((size_t)hit_chach->slot) + hit_chach->size))
        return hit_chach;

    for (i = 0; i < gc_heaps_used;  i++) {
        if ((((void *)gc_heaps[i].slot) <= ptr) &&
            ((size_t)ptr < (((size_t)gc_heaps[i].slot) + gc_heaps[i].size))) {
            hit_chach = &gc_heaps[i];
            return &gc_heaps[i];
        }
    }
    return NULL;
}

static Header *
get_header(GC_Heap *gh, void *ptr)
{
    Header *p, *pend, *pnext;

    pend = (Header *)(((size_t)gh->slot) + gh->size);
    for (p = gh->slot; p < pend; p = pnext) {
        pnext = NEXT_HEADER(p);
        if ((void *)(p+1) <= ptr && ptr < (void *)pnext) {
            return p;
        }
    }
    return NULL;
}

void
gc_init(void)
{
    long dummy;

    /* referenced bdw-gc mark_rts.c */
    dummy = 42;

    /* check stack grow */
    stack_start = ((char *)&dummy);
}

static void
set_using_stack(void)
{
    char *tmp;
    long dummy;

    /* referenced bdw-gc mark_rts.c */
    dummy = 42;

    stack_end = (char *)&dummy;
    if (stack_start > stack_end) {
        tmp = stack_start;
        stack_start = stack_end;
        stack_start--;
        stack_end = tmp;
    }
    else {
        stack_start++;
    }
}

static void gc_mark_range(char *start, char *end);

static void
gc_mark(void * ptr)
{
    GC_Heap *gh;
    Header *hdr;

    /* mark check */
    if (!(gh = is_pointer_to_heap(ptr))) return;
    if (!(hdr = get_header(gh, ptr))) return;
    if (!FL_TEST(hdr, FL_ALLOC)) return;
    if (FL_TEST(hdr, FL_MARK)) return;

    /* marking */
    FL_SET(hdr, FL_MARK);
    DEBUG(printf("mark ptr : %p, header : %p\n", ptr, hdr));

    /* mark children */
    gc_mark_range((char *)(hdr+1), (char *)NEXT_HEADER(hdr));
}

static void
gc_mark_range(char *start, char *end)
{
    char *p;

    for (p = start; p < end; p++) {
        gc_mark(*(char **)p);
    }
}

static void
gc_mark_register(void)
{
    jmp_buf env;
    size_t i;
    
    setjmp(env);
    for (i = 0; i < sizeof(env); i++) {
        gc_mark(((void **)env)[i]);
    }
}

static void
gc_mark_stack(void)
{
    set_using_stack();
    gc_mark_range(stack_start, stack_end);
}

static void
gc_sweep(void)
{
    size_t i;
    Header *p, *pend, *pnext;

    for (i = 0; i < gc_heaps_used; i++) {
        pend = (Header *)(((size_t)gc_heaps[i].slot) + gc_heaps[i].size);
        for (p = gc_heaps[i].slot; p < pend; p = NEXT_HEADER(p)) {
            if (FL_TEST(p, FL_ALLOC)) {
                if (FL_TEST(p, FL_MARK)) {
                    DEBUG(printf("mark unset : %p\n", p));
                    FL_UNSET(p, FL_MARK);
                }
                else {
                    mini_gc_free(p+1);
                }
            }
        }
    }
}

void
add_roots(void * start, void * end)
{
    void *tmp;
    if (start > end) {
        tmp = start;
        start = end;
        end = tmp;
    }
    root_ranges[root_ranges_used].start = (char *)start;
    root_ranges[root_ranges_used].end = (char *)end;
    root_ranges_used++;

    if (root_ranges_used >= ROOT_RANGES_LIMIT) {
        fputs("Root OverFlow", stderr);
        abort();
    }
}

void
garbage_collect(void)
{
    size_t i;
    void *p;

    /* marking machine context */
    gc_mark_register();
    gc_mark_stack();

    /* marking roots */
    for (i = 0; i < root_ranges_used; i++) {
        gc_mark_range(root_ranges[i].start, root_ranges[i].end);
    }

    /* sweeping */
    gc_sweep();
}


/* ========================================================================== */
/*  test                                                                      */
/* ========================================================================== */

static void
test_mini_gc_malloc_free(void)
{
    char *p1, *p2, *p3;
    size_t i;

    /* malloc check */
    p1 = (char *)mini_gc_malloc(10);
    p2 = (char *)mini_gc_malloc(10);
    p3 = (char *)mini_gc_malloc(10);
    assert(((Header *)p1-1)->size == ALIGN(10, PTRSIZE));
    assert(((Header *)p1-1)->flags == FL_ALLOC);
    assert((Header *)(((size_t)(free_list+1)) + free_list->size) == ((Header *)p3-1));

    /* free check */
    mini_gc_free(p1);
    mini_gc_free(p3);
    mini_gc_free(p2);
    assert(free_list->next_free == free_list);
    assert((void *)gc_heaps[0].slot == (void *)free_list);
    assert(gc_heaps[0].size == TINY_HEAP_SIZE);
    assert(((Header *)p1-1)->flags == 0);

    /* grow check */
    p1 = mini_gc_malloc(TINY_HEAP_SIZE+100);
    assert(gc_heaps_used == 2);
    assert(gc_heaps[1].size == (TINY_HEAP_SIZE+100));
    mini_gc_free(p1);
}

static void
test_garbage_collect(void) {
    void *p;
    p = mini_gc_malloc(100);
    assert(FL_TEST((((Header *)p)-1), FL_ALLOC));
    p = 0;
    garbage_collect();
}

static void
test_garbage_collect_load_test(void) {
    void *p;
    int i;
    for (i = 0; i < 200; i++) {
        p = mini_gc_malloc(1000000);
    }
    assert((((Header *)p)-1)->flags);
}

static void
test(void)
{
    gc_init();
    test_mini_gc_malloc_free();
    test_garbage_collect();
    test_garbage_collect_load_test();
}


int
main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "test") == 0)  test();
}
