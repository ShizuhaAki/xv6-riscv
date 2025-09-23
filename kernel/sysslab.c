#include "proc.h"
#include "slab.h"
#include "string.h"
#include "syscall.h"

// Global cache table to map cache IDs to cache pointers
#define MAX_CACHES 64
static struct kmem_cache *cache_table[MAX_CACHES];
static int next_cache_id = 0;

// Initialize cache table
void slab_syscall_init(void) {
  for (int i = 0; i < MAX_CACHES; i++) {
    cache_table[i] = 0;
  }
  next_cache_id = 0;
}

// Find a free cache ID
static int alloc_cache_id(void) {
  for (int i = 0; i < MAX_CACHES; i++) {
    if (cache_table[i] == 0) {
      return i;
    }
  }
  return -1;
}

// Create a new cache
uint64 sys_kmem_cache_create(void) {
  char name[32];
  int objsize;
  uint64 ctor_addr, dtor_addr;
  int align;

  // Get arguments
  if (argstr(0, name, sizeof(name)) < 0) return -1;
  argint(1, &objsize);
  argaddr(2, &ctor_addr);
  argaddr(3, &dtor_addr);
  argint(4, &align);

  // Create cache
  struct kmem_cache *cache =
      kmem_cache_create(name, objsize, (void (*)(void *))ctor_addr,
                        (void (*)(void *))dtor_addr, align);
  if (!cache) {
    return -1;
  }

  // Allocate cache ID
  int cache_id = alloc_cache_id();
  if (cache_id < 0) {
    kmem_cache_destroy(cache);
    return -1;
  }

  cache_table[cache_id] = cache;
  return cache_id;
}

// Allocate an object from a cache
uint64 sys_kmem_cache_alloc(void) {
  int cache_id;
  argint(0, &cache_id);

  if (cache_id < 0 || cache_id >= MAX_CACHES || !cache_table[cache_id]) {
    return 0;
  }

  void *obj = kmem_cache_alloc(cache_table[cache_id]);
  return (uint64)obj;
}

// Free an object back to a cache
uint64 sys_kmem_cache_free(void) {
  int cache_id;
  uint64 obj_addr;

  argint(0, &cache_id);
  argaddr(1, &obj_addr);

  if (cache_id < 0 || cache_id >= MAX_CACHES || !cache_table[cache_id]) {
    return -1;
  }

  kmem_cache_free(cache_table[cache_id], (void *)obj_addr);
  return 0;
}

// Destroy a cache
uint64 sys_kmem_cache_destroy(void) {
  int cache_id;
  argint(0, &cache_id);

  if (cache_id < 0 || cache_id >= MAX_CACHES || !cache_table[cache_id]) {
    return -1;
  }

  kmem_cache_destroy(cache_table[cache_id]);
  cache_table[cache_id] = 0;
  return 0;
}
