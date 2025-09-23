#include "kernel/stat.h"
#include "kernel/types.h"
#include "user/user.h"

// Simple test structure
struct test_obj {
  int id;
  char data[100];
};

// Constructor for test objects
void test_ctor(void *obj) {
  struct test_obj *t = (struct test_obj *)obj;
  t->id = 0;
  memset(t->data, 0, sizeof(t->data));
}

// Destructor for test objects
void test_dtor(void *obj) {
  struct test_obj *t = (struct test_obj *)obj;
  t->id = -1;
  memset(t->data, 0xFF, sizeof(t->data));
}

int main(int argc, char *argv[]) {
  printf("Starting slab allocator test...\n");

  // Test 1: Basic allocation and deallocation
  printf("Test 1: Basic allocation/deallocation\n");

  // Create a cache for test objects
  int cache_id = kmem_cache_create("test_cache", sizeof(struct test_obj),
                                   test_ctor, test_dtor, 0);
  if (cache_id < 0) {
    printf("Failed to create cache\n");
    exit(1);
  }
  printf("Created cache with ID: %d\n", cache_id);

  // Allocate some objects
  struct test_obj *objs[10];
  for (int i = 0; i < 10; i++) {
    objs[i] = (struct test_obj *)kmem_cache_alloc(cache_id);
    if (!objs[i]) {
      printf("Failed to allocate object %d\n", i + 1);
      exit(1);
    }
    objs[i]->id = i + 1;
    printf("Allocated object %d at %p\n", i + 1, objs[i]);
  }

  // Free some objects
  for (int i = 0; i < 5; i++) {
    if (kmem_cache_free(cache_id, objs[i]) < 0) {
      printf("Failed to free object %d\n", i + 1);
      exit(1);
    }
    printf("Freed object %d\n", i + 1);
  }

  // Allocate more objects
  for (int i = 5; i < 10; i++) {
    objs[i] = (struct test_obj *)kmem_cache_alloc(cache_id);
    if (!objs[i]) {
      printf("Failed to reallocate object %d\n", i + 1);
      exit(1);
    }
    objs[i]->id = i + 1;
    printf("Reallocated object %d at %p\n", i + 1, objs[i]);
  }

  // Free all objects
  for (int i = 0; i < 10; i++) {
    if (kmem_cache_free(cache_id, objs[i]) < 0) {
      printf("Failed to free object %d\n", i + 1);
      exit(1);
    }
    printf("Freed object %d\n", i + 1);
  }

  // Destroy cache
  if (kmem_cache_destroy(cache_id) < 0) {
    printf("Failed to destroy cache\n");
    exit(1);
  }
  printf("Cache destroyed\n");

  printf("Test 1 completed successfully\n");

  // Test 2: Multiple caches
  printf("\nTest 2: Multiple caches\n");

  int cache1 = kmem_cache_create("cache1", 32, 0, 0, 0);
  int cache2 = kmem_cache_create("cache2", 64, 0, 0, 0);

  if (cache1 < 0 || cache2 < 0) {
    printf("Failed to create multiple caches\n");
    exit(1);
  }

  void *obj1 = kmem_cache_alloc(cache1);
  void *obj2 = kmem_cache_alloc(cache2);

  if (!obj1 || !obj2) {
    printf("Failed to allocate from multiple caches\n");
    exit(1);
  }

  printf("Allocated from cache1: %p\n", obj1);
  printf("Allocated from cache2: %p\n", obj2);

  if (kmem_cache_free(cache1, obj1) < 0 || kmem_cache_free(cache2, obj2) < 0) {
    printf("Failed to free objects\n");
    exit(1);
  }

  if (kmem_cache_destroy(cache1) < 0 || kmem_cache_destroy(cache2) < 0) {
    printf("Failed to destroy caches\n");
    exit(1);
  }

  printf("Test 2 completed successfully\n");

  // Test 3: Stress test
  printf("\nTest 3: Stress test\n");

  int stress_cache = kmem_cache_create("stress", 128, 0, 0, 0);
  if (stress_cache < 0) {
    printf("Failed to create stress cache\n");
    exit(1);
  }

  void *stress_objs[50];
  for (int i = 0; i < 50; i++) {
    stress_objs[i] = kmem_cache_alloc(stress_cache);
    if (!stress_objs[i]) {
      printf("Failed to allocate stress object %d\n", i + 1);
      exit(1);
    }
  }

  for (int i = 0; i < 50; i++) {
    if (kmem_cache_free(stress_cache, stress_objs[i]) < 0) {
      printf("Failed to free stress object %d\n", i + 1);
      exit(1);
    }
  }

  if (kmem_cache_destroy(stress_cache) < 0) {
    printf("Failed to destroy stress cache\n");
    exit(1);
  }

  printf("Test 3 completed successfully\n");

  printf("\nAll slab allocator tests passed!\n");
  exit(0);
}
