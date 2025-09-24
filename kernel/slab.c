#include "slab.h"

#include "kalloc.h"
#include "printf.h"
#include "riscv.h"
#include "string.h"

// Default alignment to cache line size (64 bytes)
#define DEFAULT_ALIGN 64

// Minimum object size to avoid fragmentation
#define MIN_OBJ_SIZE 8

// Helper function to align size
static uint align_size(uint size, uint align) {
  if (align == 0) align = DEFAULT_ALIGN;
  return (size + align - 1) & ~(align - 1);
}

// Helper function to remove slab from list
static void slab_remove(struct slab **list, struct slab *slab) {
  if (*list == slab) {
    *list = slab->next;
    return;
  }

  struct slab *prev = *list;
  while (prev && prev->next != slab) {
    prev = prev->next;
  }
  if (prev) {
    prev->next = slab->next;
  }
}

// Helper function to add slab to list head
static void slab_add_head(struct slab **list, struct slab *slab) {
  slab->next = *list;
  *list = slab;
}

// Create a new slab for the given cache
static struct slab *slab_create(struct kmem_cache *cache) {
  // If object is larger than page size, we can't create a slab
  if (cache->objsize > PGSIZE) {
    return 0;
  }

  // Allocate a page from kalloc
  char *page = (char *)kalloc();
  if (!page) {
    return 0;
  }

  // Allocate slab structure
  struct slab *slab = (struct slab *)kalloc();
  if (!slab) {
    kfree(page);
    return 0;
  }

  // Initialize slab
  slab->cache = cache;
  slab->mem = page;
  slab->nr_objs = PGSIZE / cache->objsize;
  slab->nr_free = slab->nr_objs;
  slab->next = 0;

  // Build freelist - store pointers in the first 8 bytes of each object
  // This is safe because we're using the object space for freelist management
  for (uint i = 0; i < slab->nr_objs; i++) {
    char *obj = page + i * cache->objsize;
    if (i == slab->nr_objs - 1) {
      // Last object points to null
      *(void **)obj = 0;
    } else {
      // Point to next object
      *(void **)obj = (void *)(page + (i + 1) * cache->objsize);
    }
  }

  // Initialize freelist - point to the first object
  slab->freelist = (void *)page;

  return slab;
}

// Destroy a slab and return its pages to kalloc
static void slab_destroy(struct slab *slab) {
  if (!slab) return;

  // Free the object area page (freelist is part of this page)
  kfree(slab->mem);

  // Free the slab structure
  kfree(slab);
}

// Create a cache for objects of given size
struct kmem_cache *kmem_cache_create(const char *name, uint objsize,
                                     void (*ctor)(void *), void (*dtor)(void *),
                                     uint align) {
  if (!name || objsize < MIN_OBJ_SIZE) {
    return 0;
  }

  // Allocate cache structure
  struct kmem_cache *cache = (struct kmem_cache *)kalloc();
  if (!cache) {
    return 0;
  }

  // Initialize cache
  strncpy(cache->name, name, sizeof(cache->name) - 1);
  cache->name[sizeof(cache->name) - 1] = '\0';
  cache->objsize = align_size(objsize, align);
  cache->align = align;
  cache->ctor = ctor;
  cache->dtor = dtor;
  cache->partial = 0;
  cache->full = 0;
  cache->empty = 0;
  initlock(&cache->lock, cache->name);

  return cache;
}

// Destroy a cache and all its slabs
void kmem_cache_destroy(struct kmem_cache *cache) {
  if (!cache) return;

  acquire(&cache->lock);

  // Destroy all slabs in all lists
  struct slab *slab;

  // Destroy partial slabs
  while ((slab = cache->partial)) {
    cache->partial = slab->next;
    slab_destroy(slab);
  }

  // Destroy full slabs
  while ((slab = cache->full)) {
    cache->full = slab->next;
    slab_destroy(slab);
  }

  // Destroy empty slabs
  while ((slab = cache->empty)) {
    cache->empty = slab->next;
    slab_destroy(slab);
  }

  release(&cache->lock);

  // Free the cache structure itself
  kfree(cache);
}

// Allocate an object from the cache
void *kmem_cache_alloc(struct kmem_cache *cache) {
  if (!cache) return 0;

  acquire(&cache->lock);

  struct slab *slab = 0;
  void *obj = 0;

  // Fast path: try partial slabs first
  if (cache->partial) {
    slab = cache->partial;
    obj = slab->freelist;
    slab->freelist = *(void **)slab->freelist;
    slab->nr_free--;

    // If slab is now full, move to full list
    if (slab->nr_free == 0) {
      slab_remove(&cache->partial, slab);
      slab_add_head(&cache->full, slab);
    }
  }
  // Try empty slabs
  else if (cache->empty) {
    slab = cache->empty;
    obj = slab->freelist;
    slab->freelist = *(void **)slab->freelist;
    slab->nr_free--;

    // Move to partial list
    slab_remove(&cache->empty, slab);
    slab_add_head(&cache->partial, slab);
  }
  // Create new slab
  else {
    slab = slab_create(cache);
    if (!slab) {
      release(&cache->lock);
      return 0;
    }

    obj = slab->freelist;
    slab->freelist = *(void **)slab->freelist;
    slab->nr_free--;

    // Add to partial list
    slab_add_head(&cache->partial, slab);
  }

  release(&cache->lock);

  // Call constructor if provided
  if (cache->ctor) {
    cache->ctor(obj);
  }

  return obj;
}

// Free an object back to the cache
void kmem_cache_free(struct kmem_cache *cache, void *obj) {
  if (!cache || !obj) return;

  // Call destructor if provided
  if (cache->dtor) {
    cache->dtor(obj);
  }

  acquire(&cache->lock);

  // Find which slab this object belongs to
  // We'll use a simple approach: check all slabs
  struct slab *slab = 0;

  // Check partial slabs
  for (struct slab *s = cache->partial; s; s = s->next) {
    if ((char *)obj >= s->mem && (char *)obj < s->mem + PGSIZE) {
      slab = s;
      break;
    }
  }

  // Check full slabs
  if (!slab) {
    for (struct slab *s = cache->full; s; s = s->next) {
      if ((char *)obj >= s->mem && (char *)obj < s->mem + PGSIZE) {
        slab = s;
        break;
      }
    }
  }

  // Check empty slabs
  if (!slab) {
    for (struct slab *s = cache->empty; s; s = s->next) {
      if ((char *)obj >= s->mem && (char *)obj < s->mem + PGSIZE) {
        slab = s;
        break;
      }
    }
  }

  if (!slab) {
    // Object doesn't belong to any slab - this is an error
    release(&cache->lock);
    panic("kmem_cache_free: object not found in any slab");
    return;
  }

  // Add object back to freelist
  *(void **)obj = slab->freelist;
  slab->freelist = obj;
  slab->nr_free++;

  // Move slab between lists based on its state
  if (slab->nr_free == 1) {
    // Was full, now partial
    slab_remove(&cache->full, slab);
    slab_add_head(&cache->partial, slab);
  } else if (slab->nr_free == slab->nr_objs) {
    // Now empty
    slab_remove(&cache->partial, slab);
    slab_add_head(&cache->empty, slab);
  }

  release(&cache->lock);
}

void slab_test_single_simple_alloc_and_free(void) {
  struct kmem_cache *cache = kmem_cache_create("test", 1024, 0, 0, 0);
  if (!cache) {
    printf("Failed to create cache\n");
    return;
  }
  const int OBJ_NUM = 1024;

  int iter;
  for (iter = 0; iter < OBJ_NUM; iter++) {
    if (iter % 100 == 0) {
    }
    void *obj = kmem_cache_alloc(cache);
    if (!obj) {
      printf("Failed to allocate object %d\n", iter);
      return;
    }
    kmem_cache_free(cache, obj);
  }
}

void slab_test_single_batched_alloc_and_free(void) {
  struct kmem_cache *cache = kmem_cache_create("test", 64, 0, 0, 0);
  if (!cache) {
    printf("Failed to create cache\n");
    return;
  }

  const int BATCH_SIZE = 16;
  const int OBJ_NUM = 1024 / BATCH_SIZE;

  int iter;
  for (iter = 0; iter < OBJ_NUM; iter++) {
    void *obj[BATCH_SIZE];
    for (int i = 0; i < BATCH_SIZE; i++) {
      obj[i] = kmem_cache_alloc(cache);
      if (!obj[i]) {
        printf("Failed to allocate object %d in iter %d\n", i, iter);
        return;
      }
    }
    for (int i = 0; i < BATCH_SIZE; i++) {
      kmem_cache_free(cache, obj[i]);
    }
  }
}

void slab_test_single_undividible_batched_alloc_and_free(void) {
  struct kmem_cache *cache = kmem_cache_create("test", 80, 0, 0, 0);
  if (!cache) {
    printf("Failed to create cache\n");
    return;
  }

  const int BATCH_SIZE = 16;
  const int OBJ_NUM = 1024 / BATCH_SIZE;

  int iter;
  for (iter = 0; iter < OBJ_NUM; iter++) {
    void *obj[BATCH_SIZE];
    for (int i = 0; i < BATCH_SIZE; i++) {
      obj[i] = kmem_cache_alloc(cache);
      if (!obj[i]) {
        printf("Failed to allocate object %d in iter %d\n", i, iter);
        return;
      }
    }
    for (int i = 0; i < BATCH_SIZE; i++) {
      kmem_cache_free(cache, obj[i]);
    }
  }
}

void slab_test_single_big_batch_alloc_and_free(void) {
  struct kmem_cache *cache = kmem_cache_create("test", 64, 0, 0, 0);
  if (!cache) {
    printf("Failed to create cache\n");
    return;
  }

  const int BATCH_SIZE = 128;
  const int OBJ_NUM = 1024 / BATCH_SIZE;

  int iter;
  for (iter = 0; iter < OBJ_NUM; iter++) {
    void *obj[BATCH_SIZE];
    for (int i = 0; i < BATCH_SIZE; i++) {
      obj[i] = kmem_cache_alloc(cache);
      if (!obj[i]) {
        printf("Failed to allocate object %d in iter %d\n", i, iter);
        return;
      }
    }
    for (int i = 0; i < BATCH_SIZE; i++) {
      kmem_cache_free(cache, obj[i]);
    }
  }
}

void slab_test_single_huge_batch_alloc_and_free(void) {
  struct kmem_cache *cache = kmem_cache_create("test", 64, 0, 0, 0);
  if (!cache) {
    printf("Failed to create cache\n");
    return;
  }

  const int BATCH_SIZE = 1024;
  const int OBJ_NUM = 1024 / BATCH_SIZE;

  int iter;
  for (iter = 0; iter < OBJ_NUM; iter++) {
    void *obj[BATCH_SIZE];
    for (int i = 0; i < BATCH_SIZE; i++) {
      obj[i] = kmem_cache_alloc(cache);
      if (!obj[i]) {
        printf("Failed to allocate object\n");
        return;
      }
    }
    for (int i = 0; i < BATCH_SIZE; i++) {
      kmem_cache_free(cache, obj[i]);
    }
  }
}

void (*slab_single_core_test[])(void) = {
    slab_test_single_simple_alloc_and_free,
    slab_test_single_batched_alloc_and_free,
    slab_test_single_undividible_batched_alloc_and_free,
    slab_test_single_big_batch_alloc_and_free,
    // slab_test_single_huge_batch_alloc_and_free,
};

const int slab_single_core_test_num =
    sizeof(slab_single_core_test) / sizeof(slab_single_core_test[0]);

// Single thread slab test
void slab_test_single(void) {
  printf("Starting slab single-core tests...\n");
  for (int i = 0; i < slab_single_core_test_num; i++) {
    slab_single_core_test[i]();
  }
  printf("Slab single-core tests passed\n");
}

// Multi thread slab test
void slab_test_multi(void) {}