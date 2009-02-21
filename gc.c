#define DEBUG 
// #undef DEBUG 

#ifndef DEBUG
#define NDEBUG
#endif

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>


/* =========================================================================== */
/*  mini_malloc                                                                */
/* =========================================================================== */

typedef struct header {
    size_t size;
    int mark;
    struct header *next_free;
} Header;

struct gc_heap {
    Header *slot;
    size_t size;
};

#define TINY_HEAP_SIZE 0x4000
#define PTRSIZE ((int) sizeof(void *))
#define HEADER_SIZE ((int) sizeof(Header))
#define Align(x,a) (((x) + (a - 1)) & ~(a - 1))
#define HEAP_LIMIT 10000

static Header *free_list = NULL;
static struct gc_heap gc_heaps[HEAP_LIMIT];
static size_t gc_heaps_used = 0;

void mini_gc_free(void *ptr);
void * mini_gc_malloc(size_t req_size);

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

    if((p = sbrk(req_size + PTRSIZE)) == (char *)-1)
        return NULL;

    /* address alignment */
    align_p = gc_heaps[gc_heaps_used].slot = (Header *)Align((size_t)p, PTRSIZE);
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

    req_size = Align(req_size, PTRSIZE);

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
                p->size -= req_size;
                p = (Header *)((size_t)p + p->size);
                p->size = req_size;
            }
            free_list = prevp;
            assert(p->size != 0);
            return (void *)(p+1);
        }
        if (p == free_list)
            if ((p = grow(req_size)) == NULL)
                return NULL;
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

    if ((Header *)((size_t)target + target->size) == hit->next_free) {
        /* merge */
        target->size += hit->next_free->size;
        target->next_free = hit->next_free->next_free;
    }
    else {
        /* join next free block */
        target->next_free = hit->next_free;
    }
    if ((Header *)((size_t)hit + hit->size) == target) {
        /* merge */
        hit->size += target->size;
        hit->next_free = target->next_free;
    }
    else {
        /* join before free block */
        hit->next_free = target;
    }
    free_list = hit;
}

void
test(void)
{
    char *p1, *p2, *p3;
    int i;

    /* malloc check */
    p1 = (char *)mini_gc_malloc(10);
    p2 = (char *)mini_gc_malloc(10);
    p3 = (char *)mini_gc_malloc(10);
    assert(((Header *)p1-1)->size == Align(10, PTRSIZE));

    /* free check */
    mini_gc_free(p1);
    mini_gc_free(p3);
    mini_gc_free(p2);
    assert(free_list->next_free == free_list);
    assert((void *)gc_heaps[0].slot == (void *)free_list);
    assert(gc_heaps[0].size == TINY_HEAP_SIZE);

    /* grow check */
    mini_gc_malloc(TINY_HEAP_SIZE+100);
    assert(gc_heaps_used == 2);
    assert(gc_heaps[1].size == (TINY_HEAP_SIZE+100));
}

int
main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "test") == 0)  test();
}

/* =========================================================================== */
/*  mini_gc                                                                    */
/* =========================================================================== */

