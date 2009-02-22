#ifndef _MINI_GC
#define _MINI_GC

void mini_gc_free(void *ptr);
void * mini_gc_malloc(size_t req_size);

void garbage_collect(void);
void gc_init(void);
void add_roots(void * start, void * end);

#endif

