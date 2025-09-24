#pragma once

#include "spinlock.h"
#include "types.h"

// Forward declaration
struct kmem_cache;
struct slab;

// Slab structure for managing objects within a page
struct slab {
  struct slab *next;
  struct kmem_cache *cache;
  char *mem;       // slab object area start address
  uint nr_objs;    // total number of objects
  uint nr_free;    // number of free objects
  void *freelist;  // free object list (single linked list)
};

// Cache structure for each object type
struct kmem_cache {
  char name[32];
  uint objsize;  // object size (including alignment/metadata overhead)
  uint align;    // alignment (usually cacheline aligned)
  void (*ctor)(void *);
  void (*dtor)(void *);
  struct slab *partial;  // partially available slab list
  struct slab *full;     // full slab list
  struct slab *empty;    // empty slab list
  struct spinlock lock;
};

// API functions
struct kmem_cache *kmem_cache_create(const char *name, uint objsize,
                                     void (*ctor)(void *), void (*dtor)(void *),
                                     uint align);
void kmem_cache_destroy(struct kmem_cache *cache);

void *kmem_cache_alloc(struct kmem_cache *cache);
void kmem_cache_free(struct kmem_cache *cache, void *obj);

void slab_test_single(void);
void slab_test_multi(void);