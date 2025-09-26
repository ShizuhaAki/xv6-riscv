#include "slab_test_benchmark.h"

#include "../kalloc.h"
#include "../printf.h"
#include "../proc.h"
#include "../riscv.h"
#include "../slab.h"
#include "../spinlock.h"
#include "../string.h"

// Benchmark configuration
#define BENCHMARK_ITERATIONS 100000  // Reduced to prevent overload
#define SMALL_OBJ_SIZE 32
#define MEDIUM_OBJ_SIZE 128
#define LARGE_OBJ_SIZE 512
#define POOL_SIZE 256             // Reduced pool size
#define TIME_FREQ_HZ 10000000ULL  // 10MHz estimate for RISC-V timer

// Memory tracking (removed unused variables)

// Simple timing utilities
static inline uint64 read_time(void) {
  uint64 time;
  asm volatile("rdtime %0" : "=r"(time));
  return time;
}

// Simple random number generator for testing
static uint32 rand_seed = 1;

static uint32 simple_rand(void) {
  rand_seed = rand_seed * 1103515245 + 12345;
  return rand_seed;
}

static void seed_rand(uint32 seed) { rand_seed = seed; }

// Helper function to allocate a new page for the pool
static struct pool_page *pool_page_create(uint object_size) {
  struct pool_page *page = (struct pool_page *)kalloc();
  if (!page) return 0;

  // Ensure proper alignment for object_size
  if (object_size < sizeof(void *)) {
    object_size = sizeof(void *);
  }
  object_size = (object_size + sizeof(void *) - 1) & ~(sizeof(void *) - 1);

  // Calculate how many objects fit in a single page
  uint objects_per_page = 4096 / object_size;
  if (objects_per_page == 0) {
    // Object too large for single page
    kfree(page);
    return 0;
  }

  page->memory_base = kalloc();
  if (!page->memory_base) {
    kfree(page);
    return 0;
  }

  // Initialize free list - use another page for free list
  page->free_list = (void **)kalloc();
  if (!page->free_list) {
    kfree(page->memory_base);
    kfree(page);
    return 0;
  }

  page->objects_per_page = objects_per_page;
  page->free_count = objects_per_page;
  page->next = 0;

  // Setup free list pointers with bounds checking
  char *obj_ptr = (char *)page->memory_base;
  uint max_free_entries = 4096 / sizeof(void *);
  uint entries_to_create = (objects_per_page < max_free_entries)
                               ? objects_per_page
                               : max_free_entries;

  for (uint i = 0; i < entries_to_create; i++) {
    page->free_list[i] = obj_ptr;
    obj_ptr += object_size;
  }

  return page;
}

// Helper function to destroy a page
static void pool_page_destroy(struct pool_page *page) {
  if (!page) return;

  if (page->memory_base) kfree(page->memory_base);
  if (page->free_list) kfree(page->free_list);
  kfree(page);
}

// Simple object pool implementation for comparison (now supports multiple
// pages)
struct simple_object_pool *object_pool_create(uint object_size,
                                              uint pool_size) {
  struct simple_object_pool *pool = (struct simple_object_pool *)kalloc();
  if (!pool) return 0;

  // Ensure proper alignment for object_size
  if (object_size < sizeof(void *)) {
    object_size = sizeof(void *);
  }
  object_size = (object_size + sizeof(void *) - 1) & ~(sizeof(void *) - 1);

  pool->object_size = object_size;
  pool->total_allocated_count = 0;
  pool->total_free_count = 0;
  pool->page_count = 0;
  pool->pages = 0;

  // Create the first page
  struct pool_page *first_page = pool_page_create(object_size);
  if (!first_page) {
    kfree(pool);
    return 0;
  }

  pool->pages = first_page;
  pool->page_count = 1;
  pool->total_free_count = first_page->free_count;

  return pool;
}

void *object_pool_alloc(struct simple_object_pool *pool) {
  if (!pool) return 0;

  // Find a page with free objects
  struct pool_page *current_page = pool->pages;
  while (current_page) {
    if (current_page->free_count > 0) {
      // Found a page with free objects
      current_page->free_count--;
      void *obj = current_page->free_list[current_page->free_count];
      pool->total_allocated_count++;
      pool->total_free_count--;
      return obj;
    }
    current_page = current_page->next;
  }

  // All pages are full, allocate a new page
  struct pool_page *new_page = pool_page_create(pool->object_size);
  if (!new_page) {
    return 0;  // Failed to allocate new page
  }

  // Add new page to the front of the list
  new_page->next = pool->pages;
  pool->pages = new_page;
  pool->page_count++;
  pool->total_free_count += new_page->free_count;

  // Allocate from the new page
  new_page->free_count--;
  void *obj = new_page->free_list[new_page->free_count];
  pool->total_allocated_count++;
  pool->total_free_count--;

  return obj;
}

void object_pool_free(struct simple_object_pool *pool, void *obj) {
  if (!pool || !obj) return;

  // Find which page this object belongs to
  struct pool_page *current_page = pool->pages;
  while (current_page) {
    char *page_start = (char *)current_page->memory_base;
    char *page_end = page_start + 4096;  // Page size

    if ((char *)obj >= page_start && (char *)obj < page_end) {
      // Found the correct page
      if (current_page->free_count >= current_page->objects_per_page) {
        // Page is already full of free objects, something's wrong
        return;
      }

      current_page->free_list[current_page->free_count] = obj;
      current_page->free_count++;
      pool->total_allocated_count--;
      pool->total_free_count++;
      return;
    }
    current_page = current_page->next;
  }

  // Object doesn't belong to any page in this pool - ignore
}

void object_pool_destroy(struct simple_object_pool *pool) {
  if (!pool) return;

  // Destroy all pages
  struct pool_page *current_page = pool->pages;
  while (current_page) {
    struct pool_page *next_page = current_page->next;
    pool_page_destroy(current_page);
    current_page = next_page;
  }

  kfree(pool);
}

// Performance metrics calculation
void calculate_performance_metrics(uint64 start_time, uint64 end_time,
                                   int operations, uint64 memory_used,
                                   uint64 memory_overhead,
                                   struct perf_metrics *metrics) {
  metrics->total_cycles = end_time - start_time;
  metrics->total_operations = operations;
  metrics->avg_latency_cycles = metrics->total_cycles / operations;
  metrics->throughput_ops_per_sec =
      (operations * TIME_FREQ_HZ) / metrics->total_cycles;
  metrics->memory_allocated_bytes = memory_used;
  metrics->memory_overhead_bytes = memory_overhead;

  if (memory_used + memory_overhead > 0) {
    // Calculate efficiency as integer percentage * 100 (for 2 decimal places)
    metrics->memory_efficiency_percent_x100 =
        (memory_used * 10000) / (memory_used + memory_overhead);
  } else {
    metrics->memory_efficiency_percent_x100 = 0;
  }
}

void print_performance_metrics(const char *test_name,
                               struct perf_metrics *metrics) {
  printf("=== %s PERFORMANCE ===\n", test_name);
  printf("  Total Operations: %llu\n", metrics->total_operations);
  printf("  Total Cycles: %llu\n", metrics->total_cycles);
  printf("  Avg Latency: %llu cycles/op\n", metrics->avg_latency_cycles);
  printf("  Throughput: %llu ops/sec\n", metrics->throughput_ops_per_sec);
  printf("  Memory Used: %llu bytes\n", metrics->memory_allocated_bytes);
  printf("  Memory Overhead: %llu bytes\n", metrics->memory_overhead_bytes);
  printf("  Memory Efficiency: ");
  print_percent(metrics->memory_efficiency_percent_x100);
  printf("\n");
  printf("\n");
}

// Compare slab vs object pool throughput
int benchmark_compare_throughput(void) {
  printf("\n=== THROUGHPUT COMPARISON ===\n");

  struct perf_metrics slab_metrics = {0};
  struct perf_metrics pool_metrics = {0};

  // Test slab allocator
  struct kmem_cache *cache =
      kmem_cache_create("throughput", MEDIUM_OBJ_SIZE, 0, 0, 0);
  if (!cache) {
    printf("Failed to create slab cache\n");
    return 0;
  }

  uint64 start_time = read_time();
  for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
    void *obj = kmem_cache_alloc(cache);
    if (obj) {
      *(uint32 *)obj = i;  // Simulate work
      kmem_cache_free(cache, obj);
    }
  }
  uint64 end_time = read_time();

  calculate_performance_metrics(start_time, end_time, BENCHMARK_ITERATIONS * 2,
                                BENCHMARK_ITERATIONS * MEDIUM_OBJ_SIZE, 4096,
                                &slab_metrics);  // Estimate overhead

  kmem_cache_destroy(cache);

  // Test object pool with massive allocation + random deallocation
  struct simple_object_pool *pool =
      object_pool_create(MEDIUM_OBJ_SIZE, POOL_SIZE);
  if (!pool) {
    printf("Failed to create object pool\n");
    return 0;
  }

  // Allocate space for tracking allocated objects
  void **allocated_objects = (void **)kalloc();
  if (!allocated_objects) {
    printf("Failed to allocate tracking array\n");
    object_pool_destroy(pool);
    return 0;
  }

  const int max_objects =
      4096 / sizeof(void *);  // Max pointers that fit in a page
  int allocated_count = 0;

  seed_rand((uint32)read_time());  // Seed with current time

  start_time = read_time();
  int total_ops = 0;

  // Phase 1: Massive allocation
  printf("  Pool: Massive allocation phase...\n");
  for (int i = 0; i < BENCHMARK_ITERATIONS && allocated_count < max_objects;
       i++) {
    void *obj = object_pool_alloc(pool);
    if (obj) {
      allocated_objects[allocated_count] = obj;
      *(uint32 *)obj = i;  // Simulate work
      allocated_count++;
      total_ops++;
    }
  }

  // Phase 2: Random deallocation (free 60% randomly)
  printf("  Pool: Random deallocation phase (%d objects)...\n",
         allocated_count);
  int to_free = allocated_count * 6 / 10;  // Free 60%
  for (int i = 0; i < to_free; i++) {
    if (allocated_count > 0) {
      int idx = simple_rand() % allocated_count;
      if (allocated_objects[idx]) {
        object_pool_free(pool, allocated_objects[idx]);
        // Move last element to this position
        allocated_objects[idx] = allocated_objects[allocated_count - 1];
        allocated_count--;
        total_ops++;
      }
    }
  }

  // Phase 3: Mixed allocation/deallocation
  printf("  Pool: Mixed allocation/deallocation phase...\n");
  for (int round = 0; round < 10; round++) {
    // Allocate some
    for (int i = 0; i < 100 && allocated_count < max_objects; i++) {
      void *obj = object_pool_alloc(pool);
      if (obj) {
        allocated_objects[allocated_count] = obj;
        *(uint32 *)obj = round * 100 + i;
        allocated_count++;
        total_ops++;
      }
    }

    // Randomly free some
    int to_free_now = (allocated_count > 50) ? 30 : allocated_count / 3;
    for (int i = 0; i < to_free_now && allocated_count > 0; i++) {
      int idx = simple_rand() % allocated_count;
      if (allocated_objects[idx]) {
        object_pool_free(pool, allocated_objects[idx]);
        allocated_objects[idx] = allocated_objects[allocated_count - 1];
        allocated_count--;
        total_ops++;
      }
    }
  }

  end_time = read_time();

  // Clean up remaining objects
  for (int i = 0; i < allocated_count; i++) {
    if (allocated_objects[i]) {
      object_pool_free(pool, allocated_objects[i]);
    }
  }

  calculate_performance_metrics(start_time, end_time, total_ops,
                                total_ops * MEDIUM_OBJ_SIZE / 4,
                                pool->page_count * 4096, &pool_metrics);

  kfree(allocated_objects);
  object_pool_destroy(pool);

  // Print comparison
  print_performance_metrics("SLAB ALLOCATOR", &slab_metrics);
  print_performance_metrics("OBJECT POOL", &pool_metrics);

  printf("THROUGHPUT COMPARISON SUMMARY:\n");
  printf("  Slab Throughput: %llu ops/sec\n",
         slab_metrics.throughput_ops_per_sec);
  printf("  Pool Throughput: %llu ops/sec\n",
         pool_metrics.throughput_ops_per_sec);
  if (pool_metrics.throughput_ops_per_sec > 0) {
    uint64 ratio = (slab_metrics.throughput_ops_per_sec * 100) /
                   pool_metrics.throughput_ops_per_sec;
    printf("  Slab vs Pool: %llu%%\n", ratio);
  }
  printf("\n");

  return 1;
}

// Compare latency characteristics
int benchmark_compare_latency(void) {
  printf("\n=== LATENCY COMPARISON ===\n");

  const int samples = 50;  // Further reduced to prevent issues
  uint64 *slab_latencies = (uint64 *)kalloc();
  uint64 *pool_latencies = (uint64 *)kalloc();

  if (!slab_latencies || !pool_latencies) {
    printf("Failed to allocate latency arrays\n");
    if (slab_latencies) kfree(slab_latencies);
    if (pool_latencies) kfree(pool_latencies);
    return 0;
  }

  // Test slab latency
  struct kmem_cache *cache =
      kmem_cache_create("latency", MEDIUM_OBJ_SIZE, 0, 0, 0);
  if (!cache) {
    printf("Failed to create slab cache\n");
    return 0;
  }

  for (int i = 0; i < samples; i++) {
    uint64 start = read_time();
    void *obj = kmem_cache_alloc(cache);
    uint64 end = read_time();
    slab_latencies[i] = end - start;

    if (obj) kmem_cache_free(cache, obj);
  }

  kmem_cache_destroy(cache);

  // Test pool latency with random allocation/deallocation patterns
  struct simple_object_pool *pool =
      object_pool_create(MEDIUM_OBJ_SIZE, POOL_SIZE);
  if (!pool) {
    printf("Failed to create object pool\n");
    kfree(slab_latencies);
    kfree(pool_latencies);
    return 0;
  }

  // Pre-allocate some objects to create fragmentation
  void **pre_allocated = (void **)kalloc();
  if (!pre_allocated) {
    printf("Failed to allocate pre-allocation array\n");
    object_pool_destroy(pool);
    kfree(slab_latencies);
    kfree(pool_latencies);
    return 0;
  }

  seed_rand((uint32)read_time() + 12345);

  // Pre-allocate objects to stress test the pool
  int pre_alloc_count = 0;
  const int max_pre_alloc = (4096 / sizeof(void *)) / 2;

  for (int i = 0; i < max_pre_alloc; i++) {
    void *obj = object_pool_alloc(pool);
    if (obj) {
      pre_allocated[pre_alloc_count] = obj;
      *(uint32 *)obj = i;
      pre_alloc_count++;
    }
  }

  // Randomly free about half of them to create fragmentation
  int to_free = pre_alloc_count / 2;
  for (int i = 0; i < to_free; i++) {
    if (pre_alloc_count > 0) {
      int idx = simple_rand() % pre_alloc_count;
      object_pool_free(pool, pre_allocated[idx]);
      pre_allocated[idx] = pre_allocated[pre_alloc_count - 1];
      pre_alloc_count--;
    }
  }

  printf(
      "  Pool: Testing latency with %d pre-allocated objects and "
      "fragmentation...\n",
      pre_alloc_count);

  // Now measure allocation latencies in this fragmented state
  for (int i = 0; i < samples; i++) {
    uint64 start = read_time();
    void *obj = object_pool_alloc(pool);
    uint64 end = read_time();
    pool_latencies[i] = end - start;

    // Randomly decide whether to free immediately or keep for later
    if (obj) {
      if ((simple_rand() % 3) == 0) {  // 33% chance to free immediately
        object_pool_free(pool, obj);
      } else if (pre_alloc_count < max_pre_alloc) {
        // Keep it in our tracking array
        pre_allocated[pre_alloc_count] = obj;
        pre_alloc_count++;
      } else {
        object_pool_free(pool, obj);
      }
    }
  }

  // Clean up remaining pre-allocated objects
  for (int i = 0; i < pre_alloc_count; i++) {
    if (pre_allocated[i]) {
      object_pool_free(pool, pre_allocated[i]);
    }
  }

  kfree(pre_allocated);
  object_pool_destroy(pool);

  // Calculate statistics
  uint64 slab_min = ~0ULL, slab_max = 0, slab_total = 0;
  uint64 pool_min = ~0ULL, pool_max = 0, pool_total = 0;

  for (int i = 0; i < samples; i++) {
    if (slab_latencies[i] < slab_min) slab_min = slab_latencies[i];
    if (slab_latencies[i] > slab_max) slab_max = slab_latencies[i];
    slab_total += slab_latencies[i];

    if (pool_latencies[i] < pool_min) pool_min = pool_latencies[i];
    if (pool_latencies[i] > pool_max) pool_max = pool_latencies[i];
    pool_total += pool_latencies[i];
  }

  uint64 slab_avg = slab_total / samples;
  uint64 pool_avg = pool_total / ((samples < POOL_SIZE) ? samples : POOL_SIZE);

  printf("SLAB ALLOCATOR LATENCY:\n");
  printf("  Min: %llu cycles\n", slab_min);
  printf("  Max: %llu cycles\n", slab_max);
  printf("  Avg: %llu cycles\n", slab_avg);
  printf("  Range: %llu cycles\n", slab_max - slab_min);

  printf("\nOBJECT POOL LATENCY:\n");
  printf("  Min: %llu cycles\n", pool_min);
  printf("  Max: %llu cycles\n", pool_max);
  printf("  Avg: %llu cycles\n", pool_avg);
  printf("  Range: %llu cycles\n", pool_max - pool_min);

  printf("\nLATENCY COMPARISON:\n");
  printf("  Slab avg latency: %llu cycles\n", slab_avg);
  printf("  Pool avg latency: %llu cycles\n", pool_avg);
  if (pool_avg > 0) {
    uint64 ratio = (slab_avg * 100) / pool_avg;
    printf("  Slab vs Pool latency: %llu%%\n", ratio);
  }
  printf("\n");

  // Clean up allocated arrays
  kfree(slab_latencies);
  kfree(pool_latencies);

  return 1;
}

// Compare memory efficiency
int benchmark_compare_memory_efficiency(void) {
  printf("\n=== MEMORY EFFICIENCY COMPARISON ===\n");

  const int test_objects = 100;  // Reduced for safety

  // Test slab memory efficiency
  struct kmem_cache *cache =
      kmem_cache_create("memory", MEDIUM_OBJ_SIZE, 0, 0, 0);
  if (!cache) {
    printf("Failed to create slab cache\n");
    return 0;
  }

  void *slab_objects[100];
  int slab_allocated = 0;

  for (int i = 0; i < test_objects; i++) {
    slab_objects[i] = kmem_cache_alloc(cache);
    if (slab_objects[i]) slab_allocated++;
  }

  uint64 slab_payload = slab_allocated * MEDIUM_OBJ_SIZE;
  uint64 slab_overhead = 4096;  // Estimate slab metadata overhead

  // Clean up slab objects
  for (int i = 0; i < slab_allocated; i++) {
    if (slab_objects[i]) kmem_cache_free(cache, slab_objects[i]);
  }
  kmem_cache_destroy(cache);

  // Test pool memory efficiency with stress pattern
  struct simple_object_pool *pool =
      object_pool_create(MEDIUM_OBJ_SIZE, POOL_SIZE);
  if (!pool) {
    printf("Failed to create object pool\n");
    return 0;
  }

  // Allocate tracking array for pool objects
  void **pool_objects = (void **)kalloc();
  if (!pool_objects) {
    printf("Failed to allocate pool tracking array\n");
    object_pool_destroy(pool);
    return 0;
  }

  seed_rand((uint32)read_time() + 54321);

  const int max_pool_objects = 4096 / sizeof(void *);
  int pool_allocated = 0;
  int total_allocations = 0;

  printf("  Pool: Stress testing memory efficiency...\n");

  // Phase 1: Aggressive allocation to trigger page expansion
  for (int i = 0; i < test_objects * 5 && pool_allocated < max_pool_objects;
       i++) {
    void *obj = object_pool_alloc(pool);
    if (obj) {
      pool_objects[pool_allocated] = obj;
      *(uint32 *)obj = i;
      pool_allocated++;
      total_allocations++;
    }
  }

  printf("  Pool: Allocated %d objects across %d pages\n", pool_allocated,
         pool->page_count);

  // Phase 2: Random deallocation pattern to create fragmentation
  int deallocation_rounds = 3;
  for (int round = 0; round < deallocation_rounds; round++) {
    int to_free = pool_allocated / 3;  // Free 1/3 each round
    printf("  Pool: Deallocation round %d - freeing %d objects\n", round + 1,
           to_free);

    for (int i = 0; i < to_free && pool_allocated > 0; i++) {
      int idx = simple_rand() % pool_allocated;
      if (pool_objects[idx]) {
        object_pool_free(pool, pool_objects[idx]);
        // Move last element to this position
        pool_objects[idx] = pool_objects[pool_allocated - 1];
        pool_allocated--;
      }
    }

    // Reallocate some to test reuse
    int to_realloc = to_free / 2;
    for (int i = 0; i < to_realloc && pool_allocated < max_pool_objects; i++) {
      void *obj = object_pool_alloc(pool);
      if (obj) {
        pool_objects[pool_allocated] = obj;
        *(uint32 *)obj = round * 1000 + i;
        pool_allocated++;
        total_allocations++;
      }
    }
  }

  printf("  Pool: Final state - %d active objects, %d total pages\n",
         pool_allocated, pool->page_count);

  uint64 pool_payload = pool_allocated * MEDIUM_OBJ_SIZE;
  uint64 pool_overhead =
      pool->page_count * 4096 +                      // Memory pages
      pool->page_count * 4096 +                      // Free list pages
      pool->page_count * sizeof(struct pool_page) +  // Page structures
      sizeof(struct simple_object_pool);             // Pool structure

  // Clean up pool objects
  for (int i = 0; i < pool_allocated; i++) {
    if (pool_objects[i]) object_pool_free(pool, pool_objects[i]);
  }

  kfree(pool_objects);
  object_pool_destroy(pool);

  // Calculate efficiency
  uint64 slab_efficiency_x100 =
      (slab_payload * 10000) / (slab_payload + slab_overhead);
  uint64 pool_efficiency_x100 =
      (pool_payload * 10000) / (pool_payload + pool_overhead);

  printf("SLAB ALLOCATOR MEMORY:\n");
  printf("  Allocated objects: %d\n", slab_allocated);
  printf("  Payload memory: %llu bytes\n", slab_payload);
  printf("  Overhead memory: %llu bytes\n", slab_overhead);
  printf("  Total memory: %llu bytes\n", slab_payload + slab_overhead);
  printf("  Efficiency: ");
  print_percent(slab_efficiency_x100);
  printf("\n");

  printf("\nOBJECT POOL MEMORY:\n");
  printf("  Allocated objects: %d\n", pool_allocated);
  printf("  Payload memory: %llu bytes\n", pool_payload);
  printf("  Overhead memory: %llu bytes\n", pool_overhead);
  printf("  Total memory: %llu bytes\n", pool_payload + pool_overhead);
  printf("  Efficiency: ");
  print_percent(pool_efficiency_x100);
  printf("\n");

  printf("\nMEMORY EFFICIENCY COMPARISON:\n");
  printf("  Slab efficiency: ");
  print_percent(slab_efficiency_x100);
  printf("\n");
  printf("  Pool efficiency: ");
  print_percent(pool_efficiency_x100);
  printf("\n");
  printf("\n");

  return 1;
}

// Mixed workload comparison
int benchmark_compare_mixed_workload(void) {
  printf("\n=== MIXED WORKLOAD COMPARISON ===\n");

  struct perf_metrics slab_metrics = {0};
  struct perf_metrics pool_metrics = {0};

  // Slab mixed workload
  struct kmem_cache *cache =
      kmem_cache_create("mixed", MEDIUM_OBJ_SIZE, 0, 0, 0);
  if (!cache) {
    printf("Failed to create slab cache\n");
    return 0;
  }

  void *slab_objects[100];
  uint64 start_time = read_time();
  int slab_ops = 0;

  // Mixed pattern: allocate batch, use, free some, allocate more
  for (int round = 0; round < 10; round++) {  // Reduced iterations
    // Allocate batch
    for (int i = 0; i < 100 && i < 100; i++) {
      slab_objects[i] = kmem_cache_alloc(cache);
      if (slab_objects[i]) {
        *(uint32 *)slab_objects[i] = round * 100 + i;
        slab_ops++;
      }
    }

    // Free half
    for (int i = 0; i < 50; i++) {
      if (slab_objects[i]) {
        kmem_cache_free(cache, slab_objects[i]);
        slab_objects[i] = 0;
        slab_ops++;
      }
    }

    // Allocate more
    for (int i = 0; i < 50; i++) {
      if (!slab_objects[i]) {
        slab_objects[i] = kmem_cache_alloc(cache);
        if (slab_objects[i]) slab_ops++;
      }
    }

    // Free all
    for (int i = 0; i < 100; i++) {
      if (slab_objects[i]) {
        kmem_cache_free(cache, slab_objects[i]);
        slab_objects[i] = 0;
        slab_ops++;
      }
    }
  }

  uint64 end_time = read_time();
  calculate_performance_metrics(start_time, end_time, slab_ops,
                                slab_ops * MEDIUM_OBJ_SIZE / 4, 4096,
                                &slab_metrics);

  kmem_cache_destroy(cache);

  // Pool complex mixed workload with dynamic growth
  struct simple_object_pool *pool =
      object_pool_create(MEDIUM_OBJ_SIZE, POOL_SIZE);
  if (!pool) {
    printf("Failed to create object pool\n");
    return 0;
  }

  // Allocate multiple tracking arrays for different object lifetimes
  void **short_lived = (void **)kalloc();   // Objects freed quickly
  void **medium_lived = (void **)kalloc();  // Objects kept for medium term
  void **long_lived = (void **)kalloc();    // Objects kept for long term

  if (!short_lived || !medium_lived || !long_lived) {
    printf("Failed to allocate pool tracking arrays\n");
    if (short_lived) kfree(short_lived);
    if (medium_lived) kfree(medium_lived);
    if (long_lived) kfree(long_lived);
    object_pool_destroy(pool);
    return 0;
  }

  seed_rand((uint32)read_time() + 98765);

  const int max_objects_per_array = (4096 / sizeof(void *)) / 3;
  int short_count = 0, medium_count = 0, long_count = 0;

  start_time = read_time();
  int pool_ops = 0;

  printf("  Pool: Complex mixed workload with multiple object lifetimes...\n");

  for (int round = 0; round < 20; round++) {  // More rounds for stress test
    printf("  Pool: Round %d - Pages: %d, Objects: S=%d M=%d L=%d\n", round + 1,
           pool->page_count, short_count, medium_count, long_count);

    // Burst allocation phase
    int burst_size = 50 + (simple_rand() % 100);  // Random burst size
    for (int i = 0; i < burst_size; i++) {
      void *obj = object_pool_alloc(pool);
      if (obj) {
        *(uint32 *)obj = round * 1000 + i;
        pool_ops++;

        // Randomly assign lifetime based on probability
        uint32 lifetime_choice = simple_rand() % 100;
        if (lifetime_choice < 60 && short_count < max_objects_per_array) {
          // 60% short-lived (freed soon)
          short_lived[short_count++] = obj;
        } else if (lifetime_choice < 85 &&
                   medium_count < max_objects_per_array) {
          // 25% medium-lived
          medium_lived[medium_count++] = obj;
        } else if (long_count < max_objects_per_array) {
          // 15% long-lived
          long_lived[long_count++] = obj;
        } else {
          // Arrays full, free immediately
          object_pool_free(pool, obj);
          pool_ops++;
        }
      }
    }

    // Random short-lived object cleanup (80% probability)
    if ((simple_rand() % 100) < 80) {
      int to_free = short_count / 2 + (simple_rand() % (short_count / 2 + 1));
      for (int i = 0; i < to_free && short_count > 0; i++) {
        int idx = simple_rand() % short_count;
        if (short_lived[idx]) {
          object_pool_free(pool, short_lived[idx]);
          short_lived[idx] = short_lived[short_count - 1];
          short_count--;
          pool_ops++;
        }
      }
    }

    // Occasional medium-lived cleanup (30% probability)
    if ((simple_rand() % 100) < 30 && medium_count > 0) {
      int to_free = medium_count / 4;
      for (int i = 0; i < to_free && medium_count > 0; i++) {
        int idx = simple_rand() % medium_count;
        if (medium_lived[idx]) {
          object_pool_free(pool, medium_lived[idx]);
          medium_lived[idx] = medium_lived[medium_count - 1];
          medium_count--;
          pool_ops++;
        }
      }
    }

    // Rare long-lived cleanup (10% probability)
    if ((simple_rand() % 100) < 10 && long_count > 0) {
      int to_free = long_count / 6;
      for (int i = 0; i < to_free && long_count > 0; i++) {
        int idx = simple_rand() % long_count;
        if (long_lived[idx]) {
          object_pool_free(pool, long_lived[idx]);
          long_lived[idx] = long_lived[long_count - 1];
          long_count--;
          pool_ops++;
        }
      }
    }

    // Random memory pressure simulation
    if ((simple_rand() % 10) == 0) {
      // Simulate memory pressure - free more aggressively
      printf("  Pool: Simulating memory pressure...\n");
      while (short_count > 0) {
        object_pool_free(pool, short_lived[--short_count]);
        pool_ops++;
      }
      int medium_to_free = medium_count / 2;
      for (int i = 0; i < medium_to_free && medium_count > 0; i++) {
        object_pool_free(pool, medium_lived[--medium_count]);
        pool_ops++;
      }
    }
  }

  end_time = read_time();

  // Clean up all remaining objects
  printf("  Pool: Cleaning up - S=%d M=%d L=%d objects\n", short_count,
         medium_count, long_count);
  for (int i = 0; i < short_count; i++) {
    if (short_lived[i]) object_pool_free(pool, short_lived[i]);
  }
  for (int i = 0; i < medium_count; i++) {
    if (medium_lived[i]) object_pool_free(pool, medium_lived[i]);
  }
  for (int i = 0; i < long_count; i++) {
    if (long_lived[i]) object_pool_free(pool, long_lived[i]);
  }

  printf("  Pool: Final stats - %d pages used, %d total operations\n",
         pool->page_count, pool_ops);

  calculate_performance_metrics(start_time, end_time, pool_ops,
                                pool_ops * MEDIUM_OBJ_SIZE / 6,
                                pool->page_count * 4096, &pool_metrics);

  kfree(short_lived);
  kfree(medium_lived);
  kfree(long_lived);
  object_pool_destroy(pool);

  // Print comparison
  print_performance_metrics("SLAB MIXED WORKLOAD", &slab_metrics);
  print_performance_metrics("POOL MIXED WORKLOAD", &pool_metrics);

  printf("MIXED WORKLOAD SUMMARY:\n");
  printf("  Slab Operations: %llu\n", slab_metrics.total_operations);
  printf("  Pool Operations: %llu\n", pool_metrics.total_operations);
  printf("  Slab Throughput: %llu ops/sec\n",
         slab_metrics.throughput_ops_per_sec);
  printf("  Pool Throughput: %llu ops/sec\n",
         pool_metrics.throughput_ops_per_sec);
  printf("  Slab Efficiency: ");
  print_percent(slab_metrics.memory_efficiency_percent_x100);
  printf("\n");
  printf("  Pool Efficiency: ");
  print_percent(pool_metrics.memory_efficiency_percent_x100);
  printf("\n");

  return 1;
}

// Main benchmark entry point
void slab_test_benchmark(void) {
  printf("\n=== SLAB ALLOCATOR vs OBJECT POOL BENCHMARKS ===\n");
  printf("Testing with %d iterations, %d byte objects\n", BENCHMARK_ITERATIONS,
         MEDIUM_OBJ_SIZE);
  printf("Timer frequency estimate: %llu Hz\n\n", TIME_FREQ_HZ);

  // Add safety check before each test
  printf("Running throughput test...\n");
  if (!benchmark_compare_throughput()) {
    printf("Throughput test failed, skipping remaining tests\n");
    return;
  }

  printf("Running latency test...\n");
  if (!benchmark_compare_latency()) {
    printf("Latency test failed, skipping remaining tests\n");
    return;
  }

  printf("Running memory efficiency test...\n");
  if (!benchmark_compare_memory_efficiency()) {
    printf("Memory efficiency test failed, skipping remaining tests\n");
    return;
  }

  printf("Running mixed workload test...\n");
  if (!benchmark_compare_mixed_workload()) {
    printf("Mixed workload test failed\n");
    return;
  }

  printf("=== BENCHMARK COMPLETED ===\n");
  printf("Note: Results depend on system load and memory pressure.\n");
  printf("Slab allocator provides general-purpose allocation,\n");
  printf("while object pools are optimized for specific object sizes.\n\n");
}