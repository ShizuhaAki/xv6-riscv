#include "slab_test.h"

#include "../kalloc.h"
#include "../printf.h"
#include "../slab.h"

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
    void **obj = (void **)kalloc();
    if (!obj) {
      printf("Failed to allocate temp array\n");
      return;
    }
    for (int i = 0; i < BATCH_SIZE; i++) {
      obj[i] = kmem_cache_alloc(cache);
      if (!obj[i]) {
        printf("Failed to allocate object %d in iter %d\n", i, iter);
        kfree((void *)obj);
        return;
      }
    }
    for (int i = 0; i < BATCH_SIZE; i++) {
      kmem_cache_free(cache, obj[i]);
    }
    kfree((void *)obj);
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
    void **obj = (void **)kalloc();
    if (!obj) {
      printf("Failed to allocate temp array\n");
      return;
    }
    for (int i = 0; i < BATCH_SIZE; i++) {
      obj[i] = kmem_cache_alloc(cache);
      if (!obj[i]) {
        printf("Failed to allocate object %d in iter %d\n", i, iter);
        kfree((void *)obj);
        return;
      }
    }
    for (int i = 0; i < BATCH_SIZE; i++) {
      kmem_cache_free(cache, obj[i]);
    }
    kfree((void *)obj);
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
    void **obj = (void **)kalloc();
    if (!obj) {
      printf("Failed to allocate temp array\n");
      return;
    }
    for (int i = 0; i < BATCH_SIZE; i++) {
      obj[i] = kmem_cache_alloc(cache);
      if (!obj[i]) {
        printf("Failed to allocate object %d in iter %d\n", i, iter);
        kfree((void *)obj);
        return;
      }
    }
    for (int i = 0; i < BATCH_SIZE; i++) {
      kmem_cache_free(cache, obj[i]);
    }
    kfree((void *)obj);
  }
}

void slab_test_single_huge_batch_alloc_and_free(void) {
  struct kmem_cache *cache = kmem_cache_create("test", 64, 0, 0, 0);
  if (!cache) {
    printf("Failed to create cache\n");
    return;
  }

  const int BATCH_SIZE = 512;
  const int OBJ_NUM = 1024 / BATCH_SIZE;

  int iter;
  for (iter = 0; iter < OBJ_NUM; iter++) {
    void **obj = (void **)kalloc();
    if (!obj) {
      printf("Failed to allocate temp array\n");
      return;
    }
    for (int i = 0; i < BATCH_SIZE; i++) {
      obj[i] = kmem_cache_alloc(cache);
      if (!obj[i]) {
        printf("Failed to allocate object\n");
        kfree((void *)obj);
        return;
      }
    }
    for (int i = 0; i < BATCH_SIZE; i++) {
      kmem_cache_free(cache, obj[i]);
    }
    kfree((void *)obj);
  }
}

void (*slab_single_core_test[])(void) = {
    slab_test_single_simple_alloc_and_free,
    slab_test_single_batched_alloc_and_free,
    slab_test_single_undividible_batched_alloc_and_free,
    slab_test_single_big_batch_alloc_and_free,
    slab_test_single_huge_batch_alloc_and_free,
};

const int slab_single_core_test_num =
    sizeof(slab_single_core_test) / sizeof(slab_single_core_test[0]);

// Single thread slab test
void slab_test_single(void) {
  for (int i = 0; i < slab_single_core_test_num; i++) {
    slab_single_core_test[i]();
  }
  printf("Slab single-core tests passed\n");
}

// Multi thread slab test
void slab_test_multi(void) {}
