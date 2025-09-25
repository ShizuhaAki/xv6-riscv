#include "slab.h"

#include "kalloc.h"
#include "printf.h"
#include "riscv.h"
#include "string.h"

// Helper function to align size
static uint align_size(uint size, uint align) {
  if (align == 0) return size;
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
  // Allocate a page from kalloc
  char *page = (char *)kalloc();
  if (!page) {
    return 0;
  }

  uint slab_offset = align_size(sizeof(struct slab), cache->align);

  // Allocate slab structure
  struct slab *slab = (struct slab *)page;

  // Initialize slab
  slab->cache = cache;
  slab->mem = page + slab_offset;
  slab->nr_objs = (PGSIZE - slab_offset) / cache->objsize;
  slab->nr_free = slab->nr_objs;
  slab->next = 0;

  // Build freelist as a single linked list
  // Each object's first 8 bytes store pointer to next free object
  for (uint i = 0; i < slab->nr_objs; i++) {
    char *obj = slab->mem + i * cache->objsize;
    if (i == slab->nr_objs - 1) {
      // Last object points to null
      *(void **)obj = 0;
    } else {
      // Point to next object
      *(void **)obj = (void *)(slab->mem + (i + 1) * cache->objsize);
    }
  }

  // Initialize freelist - point to the first object
  slab->freelist = slab->mem;

  return slab;
}

// Destroy a slab and return its pages to kalloc
static void slab_destroy(struct slab *slab) {
  if (!slab) return;

  // Free the slab structure
  kfree(slab);
}

static inline void free_push(struct slab *s, void *obj) {
  // Validate that obj is within slab bounds
  if (!obj || (char *)obj < s->mem || (char *)obj >= s->mem + PGSIZE) {
    panic("free_push: object out of bounds");
  }

  // Ensure the object is properly aligned
  if (((uint64)obj - (uint64)s->mem) % s->cache->objsize != 0) {
    panic("free_push: object misaligned");
  }

  *(void **)obj = s->freelist;  // write next pointer to free object
  s->freelist = obj;
  s->nr_free++;
}

static inline void *free_pop(struct slab *s) {
  void *obj = s->freelist;
  if (!obj) return 0;
  s->freelist = *(void **)obj;  // read next pointer from free object
  s->nr_free--;
  return obj;
}

// Create a cache for objects of given size
struct kmem_cache *kmem_cache_create(const char *name, uint objsize,
                                     void (*ctor)(void *), void (*dtor)(void *),
                                     uint align) {
  if (!name || objsize == 0) {
    return 0;
  }

  // Align the object size first
  uint aligned_size = align_size(objsize, align);

  // Ensure the aligned size is valid
  if (aligned_size > PGSIZE - align_size(sizeof(struct slab), align)) {
    return 0;
  }

  // Ensure object is large enough to hold a freelist pointer
  if (aligned_size < sizeof(void *)) {
    aligned_size = sizeof(void *);
  }

  // Allocate cache structure
  struct kmem_cache *cache = (struct kmem_cache *)kalloc();
  if (!cache) {
    return 0;
  }

  // Initialize cache
  strncpy(cache->name, name, sizeof(cache->name) - 1);
  cache->name[sizeof(cache->name) - 1] = '\0';
  cache->objsize = aligned_size;
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
    obj = free_pop(slab);

    // If slab is now full, move to full list
    if (slab->nr_free == 0) {
      slab_remove(&cache->partial, slab);
      slab_add_head(&cache->full, slab);
    }
  }
  // Try empty slabs
  else if (cache->empty) {
    slab = cache->empty;
    obj = free_pop(slab);

    // Remove from empty regardless
    slab_remove(&cache->empty, slab);
    // Place based on remaining free count
    if (slab->nr_free == 0) {
      slab_add_head(&cache->full, slab);
    } else {
      slab_add_head(&cache->partial, slab);
    }
  }
  // Create new slab
  else {
    slab = slab_create(cache);
    if (!slab) {
      release(&cache->lock);
      return 0;
    }

    obj = free_pop(slab);

    // Place new slab based on remaining free count
    if (slab->nr_free == 0) {
      slab_add_head(&cache->full, slab);
    } else {
      slab_add_head(&cache->partial, slab);
    }
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

  // Add object back to freelist (insert at head)
  free_push(slab, obj);

  // Move slab between lists based on its state
  if (slab->nr_free == slab->nr_objs) {
    // Now empty
    slab_remove(&cache->full, slab);
    slab_remove(&cache->partial, slab);
    slab_add_head(&cache->empty, slab);
  } else if (slab->nr_free == 1) {
    // Was full, now partial
    slab_remove(&cache->full, slab);
    slab_add_head(&cache->partial, slab);
  }

  release(&cache->lock);
}
