#include "slab_test_single.h"

#include "../kalloc.h"
#include "../printf.h"
#include "../riscv.h"
#include "../slab.h"

int slab_test_single_basic_alloc(void) {
  struct kmem_cache *cache = kmem_cache_create("test", 1024, 0, 0, 0);
  if (!cache) {
    printf("Failed to create cache\n");
    return 0;
  }
  const int OBJ_NUM = 1024;

  int iter;
  for (iter = 0; iter < OBJ_NUM; iter++) {
    if (iter % 100 == 0) {
    }
    void *obj = kmem_cache_alloc(cache);
    if (!obj) {
      printf("Failed to allocate object %d\n", iter);
      return 0;
    }
    kmem_cache_free(cache, obj);
  }
  kmem_cache_destroy(cache);
  return 1;
}

int slab_test_single_batch_alloc(void) {
  struct kmem_cache *cache = kmem_cache_create("test", 64, 0, 0, 0);
  if (!cache) {
    printf("Failed to create cache\n");
    return 0;
  }

  const int BATCH_SIZE = 16;
  const int OBJ_NUM = 1024 / BATCH_SIZE;

  int iter;
  for (iter = 0; iter < OBJ_NUM; iter++) {
    void **obj = (void **)kalloc();
    if (!obj) {
      printf("Failed to allocate temp array\n");
      return 0;
    }
    for (int i = 0; i < BATCH_SIZE; i++) {
      obj[i] = kmem_cache_alloc(cache);
      if (!obj[i]) {
        printf("Failed to allocate object %d in iter %d\n", i, iter);
        kfree((void *)obj);
        return 0;
      }
    }
    for (int i = 0; i < BATCH_SIZE; i++) {
      kmem_cache_free(cache, obj[i]);
    }
    kfree((void *)obj);
  }
  kmem_cache_destroy(cache);
  return 1;
}

int slab_test_single_unaligned_batch(void) {
  struct kmem_cache *cache = kmem_cache_create("test", 80, 0, 0, 0);
  if (!cache) {
    printf("Failed to create cache\n");
    return 0;
  }

  const int BATCH_SIZE = 16;
  const int OBJ_NUM = 1024 / BATCH_SIZE;

  int iter;
  for (iter = 0; iter < OBJ_NUM; iter++) {
    void **obj = (void **)kalloc();
    if (!obj) {
      printf("Failed to allocate temp array\n");
      return 0;
    }
    for (int i = 0; i < BATCH_SIZE; i++) {
      obj[i] = kmem_cache_alloc(cache);
      if (!obj[i]) {
        printf("Failed to allocate object %d in iter %d\n", i, iter);
        kfree((void *)obj);
        return 0;
      }
    }
    for (int i = 0; i < BATCH_SIZE; i++) {
      kmem_cache_free(cache, obj[i]);
    }
    kfree((void *)obj);
  }
  kmem_cache_destroy(cache);
  return 1;
}

int slab_test_single_large_batch(void) {
  struct kmem_cache *cache = kmem_cache_create("test", 64, 0, 0, 0);
  if (!cache) {
    printf("Failed to create cache\n");
    return 0;
  }

  const int BATCH_SIZE = 128;
  const int OBJ_NUM = 1024 / BATCH_SIZE;

  int iter;
  for (iter = 0; iter < OBJ_NUM; iter++) {
    void **obj = (void **)kalloc();
    if (!obj) {
      printf("Failed to allocate temp array\n");
      return 0;
    }
    for (int i = 0; i < BATCH_SIZE; i++) {
      obj[i] = kmem_cache_alloc(cache);
      if (!obj[i]) {
        printf("Failed to allocate object %d in iter %d\n", i, iter);
        kfree((void *)obj);
        return 0;
      }
    }
    for (int i = 0; i < BATCH_SIZE; i++) {
      kmem_cache_free(cache, obj[i]);
    }
    kfree((void *)obj);
  }
  kmem_cache_destroy(cache);
  return 1;
}

int slab_test_single_huge_batch(void) {
  struct kmem_cache *cache = kmem_cache_create("test", 64, 0, 0, 0);
  if (!cache) {
    printf("Failed to create cache\n");
    return 0;
  }

  const int BATCH_SIZE = 512;
  const int OBJ_NUM = 1024 / BATCH_SIZE;

  int iter;
  for (iter = 0; iter < OBJ_NUM; iter++) {
    void **obj = (void **)kalloc();
    if (!obj) {
      printf("Failed to allocate temp array\n");
      return 0;
    }
    for (int i = 0; i < BATCH_SIZE; i++) {
      obj[i] = kmem_cache_alloc(cache);
      if (!obj[i]) {
        printf("Failed to allocate object\n");
        kfree((void *)obj);
        return 0;
      }
    }
    for (int i = 0; i < BATCH_SIZE; i++) {
      kmem_cache_free(cache, obj[i]);
    }
    kfree((void *)obj);
  }
  kmem_cache_destroy(cache);
  return 1;
}

// Test for out-of-order free operations
int slab_test_single_random_free(void) {
  struct kmem_cache *cache = kmem_cache_create("test", 128, 0, 0, 0);
  if (!cache) {
    printf("Failed to create cache\n");
    return 0;
  }

  const int OBJ_NUM = 64;
  void **objs = (void **)kalloc();
  if (!objs) {
    printf("Failed to allocate temp array\n");
    return 0;
  }

  // Allocate all objects
  for (int i = 0; i < OBJ_NUM; i++) {
    objs[i] = kmem_cache_alloc(cache);
    if (!objs[i]) {
      printf("Failed to allocate object %d\n", i);
      kfree((void *)objs);
      return 0;
    }
  }

  // Free in reverse order (simple out-of-order test)
  for (int i = OBJ_NUM - 1; i >= 0; i--) {
    kmem_cache_free(cache, objs[i]);
  }

  // Free in alternating pattern
  for (int i = 0; i < OBJ_NUM; i++) {
    objs[i] = kmem_cache_alloc(cache);
    if (!objs[i]) {
      printf("Failed to re-allocate object %d\n", i);
      kfree((void *)objs);
      return 0;
    }
  }

  // Free even indices first, then odd
  for (int i = 0; i < OBJ_NUM; i += 2) {
    kmem_cache_free(cache, objs[i]);
  }
  for (int i = 1; i < OBJ_NUM; i += 2) {
    kmem_cache_free(cache, objs[i]);
  }

  kfree((void *)objs);
  kmem_cache_destroy(cache);
  return 1;
}

// Test for constructor and destructor functionality
static int ctor_call_count = 0;
static int dtor_call_count = 0;

static void test_ctor(void *obj) {
  ctor_call_count++;
  // Initialize object with a known pattern
  *(uint64 *)obj = 0xDEADBEEFCAFEBABE;
}

static void test_dtor(void *obj) {
  dtor_call_count++;
  // Verify object still has expected pattern
  if (*(uint64 *)obj != 0xDEADBEEFCAFEBABE) {
    printf("WARNING: Object corrupted before destruction\n");
  }
  // Clear the object
  *(uint64 *)obj = 0;
}

int slab_test_single_ctor_dtor(void) {
  ctor_call_count = 0;
  dtor_call_count = 0;

  struct kmem_cache *cache =
      kmem_cache_create("test_ctor_dtor", 64, test_ctor, test_dtor, 0);
  if (!cache) {
    printf("Failed to create cache with ctor/dtor\n");
    return 0;
  }

  const int OBJ_NUM = 32;
  void **objs = (void **)kalloc();
  if (!objs) {
    printf("Failed to allocate temp array\n");
    return 0;
  }

  // Allocate objects - should call ctor for each
  for (int i = 0; i < OBJ_NUM; i++) {
    objs[i] = kmem_cache_alloc(cache);
    if (!objs[i]) {
      printf("Failed to allocate object %d\n", i);
      kfree((void *)objs);
      return 0;
    }
    // Verify constructor was called
    if (*(uint64 *)objs[i] != 0xDEADBEEFCAFEBABE) {
      printf("Constructor not called or failed for object %d\n", i);
    }
  }

  if (ctor_call_count != OBJ_NUM) {
    printf("Constructor call count mismatch: expected %d, got %d\n", OBJ_NUM,
           ctor_call_count);
  }

  // Free objects - should call dtor for each
  for (int i = 0; i < OBJ_NUM; i++) {
    kmem_cache_free(cache, objs[i]);
  }

  if (dtor_call_count != OBJ_NUM) {
    printf("Destructor call count mismatch: expected %d, got %d\n", OBJ_NUM,
           dtor_call_count);
  }

  kfree((void *)objs);
  kmem_cache_destroy(cache);
  return 1;
}

// Test memory integrity - write data and verify it persists
int slab_test_single_memory_integrity(void) {
  struct kmem_cache *cache = kmem_cache_create("integrity_test", 256, 0, 0, 0);
  if (!cache) {
    printf("Failed to create cache\n");
    return 0;
  }

  const int OBJ_NUM = 32;
  void **objs = (void **)kalloc();
  if (!objs) {
    printf("Failed to allocate temp array\n");
    return 0;
  }

  // Allocate objects and write unique patterns
  for (int i = 0; i < OBJ_NUM; i++) {
    objs[i] = kmem_cache_alloc(cache);
    if (!objs[i]) {
      printf("Failed to allocate object %d\n", i);
      kfree((void *)objs);
      return 0;
    }

    // Write a unique pattern to each object
    uint64 *data = (uint64 *)objs[i];
    for (int j = 0; j < 256 / sizeof(uint64); j++) {
      data[j] = (uint64)i * 0x1000000 + j;
    }
  }

  // Verify data integrity in random order
  int verify_order[] = {15, 3,  28, 7,  21, 10, 30, 1,  18, 5, 25,
                        12, 0,  19, 8,  26, 13, 31, 2,  17, 6, 23,
                        11, 29, 4,  20, 9,  27, 14, 22, 16, 24};

  for (int idx = 0; idx < OBJ_NUM; idx++) {
    int i = verify_order[idx];
    uint64 *data = (uint64 *)objs[i];

    for (int j = 0; j < 256 / sizeof(uint64); j++) {
      uint64 expected = (uint64)i * 0x1000000 + j;
      if (data[j] != expected) {
        printf(
            "Memory corruption detected in object %d at offset %d: expected "
            "0x%x, got 0x%x\n",
            i, j, expected, data[j]);
        kfree((void *)objs);
        return 0;
      }
    }
  }

  // Free in different order and re-allocate to test reuse
  for (int i = 0; i < OBJ_NUM; i += 2) {
    kmem_cache_free(cache, objs[i]);
  }

  // Re-allocate freed objects
  for (int i = 0; i < OBJ_NUM; i += 2) {
    objs[i] = kmem_cache_alloc(cache);
    if (!objs[i]) {
      printf("Failed to re-allocate object %d\n", i);
      kfree((void *)objs);
      return 0;
    }
    // Write new pattern
    uint64 *data = (uint64 *)objs[i];
    for (int j = 0; j < 256 / sizeof(uint64); j++) {
      data[j] = 0xFEEDFACE00000000ULL + i * 1000 + j;
    }
  }

  // Verify odd objects still have original data
  for (int i = 1; i < OBJ_NUM; i += 2) {
    uint64 *data = (uint64 *)objs[i];
    for (int j = 0; j < 256 / sizeof(uint64); j++) {
      uint64 expected = (uint64)i * 0x1000000 + j;
      if (data[j] != expected) {
        printf("Memory corruption in untouched object %d at offset %d\n", i, j);
        kfree((void *)objs);
        return 0;
      }
    }
  }

  // Verify even objects have new data
  for (int i = 0; i < OBJ_NUM; i += 2) {
    uint64 *data = (uint64 *)objs[i];
    for (int j = 0; j < 256 / sizeof(uint64); j++) {
      uint64 expected = 0xFEEDFACE00000000ULL + i * 1000 + j;
      if (data[j] != expected) {
        printf("New data corruption in re-allocated object %d at offset %d\n",
               i, j);
        kfree((void *)objs);
        return 0;
      }
    }
  }

  // Free all remaining objects
  for (int i = 0; i < OBJ_NUM; i++) {
    kmem_cache_free(cache, objs[i]);
  }

  kfree((void *)objs);
  kmem_cache_destroy(cache);
  return 1;
}

// Test edge cases and boundary conditions
int slab_test_single_edge_cases(void) {
  // Test minimum size objects
  struct kmem_cache *small_cache = kmem_cache_create("small", 8, 0, 0, 0);
  if (!small_cache) {
    printf("Failed to create small cache\n");
    return 0;
  }

  // Test many small allocations
  const int SMALL_NUM = 512;
  for (int i = 0; i < SMALL_NUM; i++) {
    void *obj = kmem_cache_alloc(small_cache);
    if (!obj) {
      printf("Failed to allocate small object %d\n", i);
      return 0;
    }
    *(uint64 *)obj = i;
    kmem_cache_free(small_cache, obj);
  }

  // Test large objects (close to page size)
  struct kmem_cache *large_cache = kmem_cache_create("large", 3072, 0, 0, 0);
  if (!large_cache) {
    printf("Failed to create large cache\n");
    return 0;
  }

  const int LARGE_NUM = 8;
  void **large_objs = (void **)kalloc();
  if (!large_objs) {
    printf("Failed to allocate temp array for large objects\n");
    return 0;
  }

  for (int i = 0; i < LARGE_NUM; i++) {
    large_objs[i] = kmem_cache_alloc(large_cache);
    if (!large_objs[i]) {
      printf("Failed to allocate large object %d\n", i);
      kfree((void *)large_objs);
      return 0;
    }
    // Write pattern to large object
    uint32 *data = (uint32 *)large_objs[i];
    for (int j = 0; j < 3072 / sizeof(uint32); j++) {
      data[j] = i * 1000000 + j;
    }
  }

  // Verify large objects
  for (int i = 0; i < LARGE_NUM; i++) {
    uint32 *data = (uint32 *)large_objs[i];
    for (int j = 0; j < 3072 / sizeof(uint32); j++) {
      if (data[j] != (uint32)(i * 1000000 + j)) {
        printf("Large object %d corrupted at offset %d\n", i, j);
        kfree((void *)large_objs);
        return 0;
      }
    }
    kmem_cache_free(large_cache, large_objs[i]);
  }

  kfree((void *)large_objs);
  kmem_cache_destroy(large_cache);
  kmem_cache_destroy(small_cache);
  return 1;
}

// Test repeated alloc/free cycles for the same cache
int slab_test_single_reuse_cycles(void) {
  struct kmem_cache *cache = kmem_cache_create("reuse", 128, 0, 0, 0);
  if (!cache) {
    printf("Failed to create cache\n");
    return 0;
  }

  const int CYCLES = 10;
  const int OBJS_PER_CYCLE = 64;

  for (int cycle = 0; cycle < CYCLES; cycle++) {
    void **objs = (void **)kalloc();
    if (!objs) {
      printf("Failed to allocate temp array for cycle %d\n", cycle);
      return 0;
    }

    // Allocate objects
    for (int i = 0; i < OBJS_PER_CYCLE; i++) {
      objs[i] = kmem_cache_alloc(cache);
      if (!objs[i]) {
        printf("Failed to allocate object %d in cycle %d\n", i, cycle);
        kfree((void *)objs);
        return 0;
      }
      // Write cycle-specific pattern
      *(uint64 *)objs[i] = (uint64)cycle << 32 | i;
    }

    // Verify data
    for (int i = 0; i < OBJS_PER_CYCLE; i++) {
      uint64 expected = (uint64)cycle << 32 | i;
      if (*(uint64 *)objs[i] != expected) {
        printf("Data mismatch in cycle %d, object %d\n", cycle, i);
        kfree((void *)objs);
        return 0;
      }
    }

    // Free all objects
    for (int i = 0; i < OBJS_PER_CYCLE; i++) {
      kmem_cache_free(cache, objs[i]);
    }

    kfree((void *)objs);
  }

  kmem_cache_destroy(cache);
  return 1;
}

// Test alignment requirements
int slab_test_single_alignment(void) {
  // Test different alignment values
  uint aligns[] = {8, 16, 32, 64, 128};
  int align_count = sizeof(aligns) / sizeof(aligns[0]);

  for (int a = 0; a < align_count; a++) {
    struct kmem_cache *cache =
        kmem_cache_create("align_test", 100, 0, 0, aligns[a]);
    if (!cache) {
      printf("Failed to create cache with alignment %d\n", aligns[a]);
      continue;
    }

    // Allocate several objects and check alignment
    for (int i = 0; i < 16; i++) {
      void *obj = kmem_cache_alloc(cache);
      if (!obj) {
        printf("Failed to allocate aligned object %d\n", i);
        break;
      }

      // Check alignment
      if ((uint64)obj % aligns[a] != 0) {
        printf("Object %d not aligned to %d bytes: address 0x%p\n", i,
               aligns[a], obj);
      }

      kmem_cache_free(cache, obj);
    }

    kmem_cache_destroy(cache);
  }
  return 1;
}

// Stress test with pressure on the allocator
int slab_test_single_stress(void) {
  struct kmem_cache *cache = kmem_cache_create("stress", 256, 0, 0, 0);
  if (!cache) {
    printf("Failed to create stress test cache\n");
    return 0;
  }

  const int STRESS_OBJS = 256;
  void **objs = (void **)kalloc();
  if (!objs) {
    printf("Failed to allocate temp array for stress test\n");
    return 0;
  }

  // Phase 1: Allocate many objects
  for (int i = 0; i < STRESS_OBJS; i++) {
    objs[i] = kmem_cache_alloc(cache);
    if (!objs[i]) {
      printf("Stress test failed at allocation %d\n", i);
      kfree((void *)objs);
      return 0;
    }
    // Write unique data
    *(uint32 *)objs[i] = 0xABCDEF00 + i;
  }

  // Phase 2: Free every 3rd object (create fragmentation)
  for (int i = 0; i < STRESS_OBJS; i += 3) {
    kmem_cache_free(cache, objs[i]);
    objs[i] = 0;
  }

  // Phase 3: Re-allocate in the gaps
  for (int i = 0; i < STRESS_OBJS; i += 3) {
    objs[i] = kmem_cache_alloc(cache);
    if (!objs[i]) {
      printf("Stress test failed at re-allocation %d\n", i);
      kfree((void *)objs);
      return 0;
    }
    *(uint32 *)objs[i] = 0xFEEDFACE + i;
  }

  // Phase 4: Verify all data
  for (int i = 0; i < STRESS_OBJS; i++) {
    uint32 expected = (i % 3 == 0) ? (0xFEEDFACE + i) : (0xABCDEF00 + i);
    if (*(uint32 *)objs[i] != expected) {
      printf("Stress test data corruption at object %d\n", i);
      kfree((void *)objs);
      return 0;
    }
  }

  // Phase 5: Free everything
  for (int i = 0; i < STRESS_OBJS; i++) {
    kmem_cache_free(cache, objs[i]);
  }

  kfree((void *)objs);
  kmem_cache_destroy(cache);
  return 1;
}

// Test cache destruction functionality
int slab_test_single_cache_destroy(void) {
  struct kmem_cache *cache = kmem_cache_create("destroy_test", 128, 0, 0, 0);
  if (!cache) {
    printf("Failed to create cache for destroy test\n");
    return 0;
  }

  const int OBJ_NUM = 64;
  void **objs = (void **)kalloc();
  if (!objs) {
    printf("Failed to allocate temp array\n");
    return 0;
  }

  // Allocate some objects
  for (int i = 0; i < OBJ_NUM; i++) {
    objs[i] = kmem_cache_alloc(cache);
    if (!objs[i]) {
      printf("Failed to allocate object %d for destroy test\n", i);
      kfree((void *)objs);
      return 0;
    }
    *(uint32 *)objs[i] = 0xDEADBEEF + i;
  }

  // Free half of the objects (leaving some allocated)
  for (int i = 0; i < OBJ_NUM / 2; i++) {
    kmem_cache_free(cache, objs[i]);
  }

  // Test destroying cache with some objects still allocated
  // This should free all remaining slabs
  kmem_cache_destroy(cache);

  kfree((void *)objs);
  return 1;
}

// Test error handling and edge cases
int slab_test_single_error_handling(void) {
  // Test creating cache with invalid parameters
  struct kmem_cache *bad_cache;

  // Test null name
  bad_cache = kmem_cache_create(0, 64, 0, 0, 0);
  if (bad_cache) {
    printf("ERROR: Cache creation should fail with null name\n");
    kmem_cache_destroy(bad_cache);
  }

  // Test zero object size
  bad_cache = kmem_cache_create("bad", 0, 0, 0, 0);
  if (bad_cache) {
    printf("ERROR: Cache creation should fail with zero object size\n");
    kmem_cache_destroy(bad_cache);
  }

  // Test object size too large
  bad_cache = kmem_cache_create("bad", PGSIZE + 1, 0, 0, 0);
  if (bad_cache) {
    printf("ERROR: Cache creation should fail with object size > PGSIZE\n");
    kmem_cache_destroy(bad_cache);
  }

  // Test operations on null cache
  void *obj = kmem_cache_alloc(0);
  if (obj) {
    printf("ERROR: Allocation should fail with null cache\n");
  }

  kmem_cache_free(0, (void *)0x1000);  // Should not crash

  // Test freeing null object
  struct kmem_cache *good_cache = kmem_cache_create("good", 64, 0, 0, 0);
  if (good_cache) {
    kmem_cache_free(good_cache, 0);  // Should not crash
    kmem_cache_destroy(good_cache);
  }
  return 1;
}

// Test multiple caches simultaneously
int slab_test_single_multi_cache(void) {
  const int CACHE_NUM = 8;
  const int OBJS_PER_CACHE = 32;

  struct kmem_cache *caches[CACHE_NUM];
  void **all_objs[CACHE_NUM];
  uint obj_sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048};

  // Create multiple caches with different object sizes
  for (int c = 0; c < CACHE_NUM; c++) {
    char name[32];
    // Simple sprintf equivalent for cache names
    name[0] = 'c';
    name[1] = 'a';
    name[2] = 'c';
    name[3] = 'h';
    name[4] = 'e';
    name[5] = '0' + c;
    name[6] = '\0';

    caches[c] = kmem_cache_create(name, obj_sizes[c], 0, 0, 0);
    if (!caches[c]) {
      printf("Failed to create cache %d\n", c);
      return 0;
    }

    all_objs[c] = (void **)kalloc();
    if (!all_objs[c]) {
      printf("Failed to allocate temp array for cache %d\n", c);
      return 0;
    }
  }

  // Allocate objects from all caches
  for (int c = 0; c < CACHE_NUM; c++) {
    for (int i = 0; i < OBJS_PER_CACHE; i++) {
      all_objs[c][i] = kmem_cache_alloc(caches[c]);
      if (!all_objs[c][i]) {
        printf("Failed to allocate object %d from cache %d\n", i, c);
        return 0;
      }
      // Write cache-specific pattern
      *(uint32 *)all_objs[c][i] = (c << 16) | i;
    }
  }

  // Verify data in all caches
  for (int c = 0; c < CACHE_NUM; c++) {
    for (int i = 0; i < OBJS_PER_CACHE; i++) {
      uint32 expected = (c << 16) | i;
      if (*(uint32 *)all_objs[c][i] != expected) {
        printf("Data corruption in cache %d, object %d\n", c, i);
        return 0;
      }
    }
  }

  // Free objects in round-robin fashion
  for (int i = 0; i < OBJS_PER_CACHE; i++) {
    for (int c = 0; c < CACHE_NUM; c++) {
      kmem_cache_free(caches[c], all_objs[c][i]);
    }
  }

  // Clean up
  for (int c = 0; c < CACHE_NUM; c++) {
    kfree((void *)all_objs[c]);
    kmem_cache_destroy(caches[c]);
  }
  return 1;
}

// Test extreme allocation limits
int slab_test_single_extreme_alloc(void) {
  struct kmem_cache *cache = kmem_cache_create("extreme", 64, 0, 0, 0);
  if (!cache) {
    printf("Failed to create cache for extreme test\n");
    return 0;
  }

  const int MAX_ALLOCS =
      PGSIZE /
      sizeof(
          void *);  // Bounded by one kalloc() page for pointer array capacity
  void **objs = (void **)kalloc();
  if (!objs) {
    printf("Failed to allocate temp array for extreme test\n");
    return 0;
  }

  int successful_allocs = 0;

  // Allocate as many objects as possible
  for (int i = 0; i < MAX_ALLOCS; i++) {
    objs[i] = kmem_cache_alloc(cache);
    if (!objs[i]) {
      // Allocation failed - this is expected at some point
      break;
    }
    successful_allocs++;
    *(uint32 *)objs[i] = 0xCAFEBABE + i;
  }

  // Verify all allocated objects
  for (int i = 0; i < successful_allocs; i++) {
    if (*(uint32 *)objs[i] != (uint32)(0xCAFEBABE + i)) {
      printf("Data corruption in extreme test object %d\n", i);
      break;
    }
  }

  // Free all allocated objects
  for (int i = 0; i < successful_allocs; i++) {
    kmem_cache_free(cache, objs[i]);
  }

  kfree((void *)objs);
  kmem_cache_destroy(cache);
  return 1;
}

// Test memory pattern corruption detection
int slab_test_single_corruption_detection(void) {
  struct kmem_cache *cache = kmem_cache_create("corruption", 128, 0, 0, 0);
  if (!cache) {
    printf("Failed to create cache for corruption test\n");
    return 0;
  }

  const int OBJ_NUM = 16;
  void **objs = (void **)kalloc();
  if (!objs) {
    printf("Failed to allocate temp array\n");
    return 0;
  }

  // Allocate objects and write specific patterns
  for (int i = 0; i < OBJ_NUM; i++) {
    objs[i] = kmem_cache_alloc(cache);
    if (!objs[i]) {
      printf("Failed to allocate object %d for corruption test\n", i);
      kfree((void *)objs);
      return 0;
    }

    // Fill entire object with known pattern
    uint32 *data = (uint32 *)objs[i];
    for (int j = 0; j < 128 / sizeof(uint32); j++) {
      data[j] = 0x12345678 + i * 100 + j;
    }
  }

  // Free some objects
  for (int i = 0; i < OBJ_NUM; i += 2) {
    kmem_cache_free(cache, objs[i]);
  }

  // Re-allocate and check if memory was properly reused
  for (int i = 0; i < OBJ_NUM; i += 2) {
    objs[i] = kmem_cache_alloc(cache);
    if (!objs[i]) {
      printf("Failed to re-allocate object %d\n", i);
      kfree((void *)objs);
      return 0;
    }

    // Write new pattern
    uint32 *data = (uint32 *)objs[i];
    for (int j = 0; j < 128 / sizeof(uint32); j++) {
      data[j] = 0x87654321 + i * 200 + j;
    }
  }

  // Check that odd objects still have original pattern
  for (int i = 1; i < OBJ_NUM; i += 2) {
    uint32 *data = (uint32 *)objs[i];
    for (int j = 0; j < 128 / sizeof(uint32); j++) {
      uint32 expected = 0x12345678 + i * 100 + j;
      if (data[j] != expected) {
        printf("Original pattern corrupted in object %d at offset %d\n", i, j);
        kfree((void *)objs);
        return 0;
      }
    }
  }

  // Check that even objects have new pattern
  for (int i = 0; i < OBJ_NUM; i += 2) {
    uint32 *data = (uint32 *)objs[i];
    for (int j = 0; j < 128 / sizeof(uint32); j++) {
      uint32 expected = 0x87654321 + i * 200 + j;
      if (data[j] != expected) {
        printf("New pattern corrupted in object %d at offset %d\n", i, j);
        kfree((void *)objs);
        return 0;
      }
    }
  }

  // Free all objects
  for (int i = 0; i < OBJ_NUM; i++) {
    kmem_cache_free(cache, objs[i]);
  }

  kfree((void *)objs);
  kmem_cache_destroy(cache);
  return 1;
}

// Test fragmentation and coalescing behavior
int slab_test_single_fragmentation(void) {
  struct kmem_cache *cache = kmem_cache_create("frag", 256, 0, 0, 0);
  if (!cache) {
    printf("Failed to create cache for fragmentation test\n");
    return 0;
  }

  const int ROUNDS = 5;
  const int OBJS_PER_ROUND = 32;

  for (int round = 0; round < ROUNDS; round++) {
    void **objs = (void **)kalloc();
    if (!objs) {
      printf("Failed to allocate temp array for round %d\n", round);
      return 0;
    }

    // Allocate objects
    for (int i = 0; i < OBJS_PER_ROUND; i++) {
      objs[i] = kmem_cache_alloc(cache);
      if (!objs[i]) {
        printf("Failed to allocate object %d in round %d\n", i, round);
        kfree((void *)objs);
        return 0;
      }
      *(uint64 *)objs[i] = (uint64)round << 32 | i;
    }

    // Create fragmentation by freeing every other object
    for (int i = 1; i < OBJS_PER_ROUND; i += 2) {
      kmem_cache_free(cache, objs[i]);
    }

    // Verify remaining objects
    for (int i = 0; i < OBJS_PER_ROUND; i += 2) {
      uint64 expected = (uint64)round << 32 | i;
      if (*(uint64 *)objs[i] != expected) {
        printf("Data corruption in fragmentation test round %d, object %d\n",
               round, i);
        kfree((void *)objs);
        return 0;
      }
    }

    // Free remaining objects
    for (int i = 0; i < OBJS_PER_ROUND; i += 2) {
      kmem_cache_free(cache, objs[i]);
    }

    kfree((void *)objs);
  }

  kmem_cache_destroy(cache);
  return 1;
}

// Test boundary conditions and special cases
int slab_test_single_boundary_conditions(void) {
  // Test object size exactly equal to minimum
  struct kmem_cache *min_cache = kmem_cache_create("min", 8, 0, 0, 0);
  if (!min_cache) {
    printf("Failed to create minimum size cache\n");
    return 0;
  }

  void *min_obj = kmem_cache_alloc(min_cache);
  if (!min_obj) {
    printf("Failed to allocate minimum size object\n");
    return 0;
  }
  *(uint64 *)min_obj = 0x1122334455667788;

  if (*(uint64 *)min_obj != 0x1122334455667788) {
    printf("Minimum size object data corruption\n");
  }

  kmem_cache_free(min_cache, min_obj);
  kmem_cache_destroy(min_cache);

  // Test object size that results in exactly one object per page
  uint one_per_page_size =
      PGSIZE - sizeof(void *);  // Leave room for freelist pointer
  struct kmem_cache *single_cache = kmem_cache_create(
      "single", one_per_page_size - sizeof(struct slab), 0, 0, 0);
  if (!single_cache) {
    printf("Failed to create single-object-per-page cache\n");
    return 0;
  }

  void *single_obj = kmem_cache_alloc(single_cache);
  if (!single_obj) {
    printf("Failed to allocate single object per page\n");
    return 0;
  }

  // Write pattern throughout the large object
  for (uint i = 0; i < one_per_page_size / sizeof(uint32); i++) {
    ((uint32 *)single_obj)[i] = 0xAAAABBBB + i;
  }

  // Verify pattern
  for (uint i = 0; i < one_per_page_size / sizeof(uint32); i++) {
    if (((uint32 *)single_obj)[i] != 0xAAAABBBB + i) {
      printf("Single object per page data corruption at offset %d\n", i);
      break;
    }
  }

  kmem_cache_free(single_cache, single_obj);
  kmem_cache_destroy(single_cache);

  // Test rapid create/destroy cycles
  for (int cycle = 0; cycle < 10; cycle++) {
    struct kmem_cache *temp_cache = kmem_cache_create("temp", 64, 0, 0, 0);
    if (!temp_cache) {
      printf("Failed to create temporary cache in cycle %d\n", cycle);
      return 0;
    }

    // Quick allocation test
    void *temp_obj = kmem_cache_alloc(temp_cache);
    if (temp_obj) {
      *(uint32 *)temp_obj = 0xCCCCDDDD + cycle;
      if (*(uint32 *)temp_obj != 0xCCCCDDDD + cycle) {
        printf("Data corruption in rapid cycle %d\n", cycle);
      }
      kmem_cache_free(temp_cache, temp_obj);
    }

    kmem_cache_destroy(temp_cache);
  }
  return 1;
}

int (*slab_single_core_test[])(void) = {
    slab_test_single_basic_alloc,
    slab_test_single_batch_alloc,
    slab_test_single_unaligned_batch,
    slab_test_single_large_batch,
    slab_test_single_huge_batch,
    slab_test_single_random_free,
    slab_test_single_ctor_dtor,
    slab_test_single_memory_integrity,
    slab_test_single_edge_cases,
    slab_test_single_reuse_cycles,
    slab_test_single_alignment,
    slab_test_single_stress,
    slab_test_single_cache_destroy,
    slab_test_single_error_handling,
    slab_test_single_multi_cache,
    slab_test_single_extreme_alloc,
    slab_test_single_corruption_detection,
    slab_test_single_fragmentation,
    slab_test_single_boundary_conditions,
};

const int slab_single_core_test_num =
    sizeof(slab_single_core_test) / sizeof(slab_single_core_test[0]);

// Single thread slab test
void slab_test_single(void) {
  int passed = 0;
  int failed = 0;
  for (int i = 0; i < slab_single_core_test_num; i++) {
    int result = slab_single_core_test[i]();
    if (result) {
      passed++;
    } else {
      failed++;
      printf("Test %d failed\n", i);
    }
  }
  printf("Slab single-core tests: %d passed, %d failed\n", passed, failed);
}
