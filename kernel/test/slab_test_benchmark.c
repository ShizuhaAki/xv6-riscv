#include "slab_test_benchmark.h"

#include "../kalloc.h"
#include "../printf.h"
#include "../proc.h"
#include "../riscv.h"
#include "../slab.h"
#include "../spinlock.h"
#include "../string.h"

// Benchmark configuration
#define BENCHMARK_ITERATIONS 1000  // Reduced to prevent overload
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

// Simple object pool implementation for comparison
struct simple_object_pool *object_pool_create(uint object_size,
                                              uint pool_size) {
  struct simple_object_pool *pool = (struct simple_object_pool *)kalloc();
  if (!pool) return 0;

  // Ensure proper alignment for object_size
  if (object_size < sizeof(void *)) {
    object_size = sizeof(void *);
  }
  object_size = (object_size + sizeof(void *) - 1) & ~(sizeof(void *) - 1);

  // Calculate how many objects fit in a single page
  uint objects_per_page = 4096 / object_size;
  if (objects_per_page == 0) {
    // Object too large for single page
    kfree(pool);
    return 0;
  }

  // Limit pool size to what fits in one page
  if (pool_size > objects_per_page) {
    pool_size = objects_per_page;
  }

  pool->memory_base = kalloc();
  if (!pool->memory_base) {
    kfree(pool);
    return 0;
  }

  pool->object_size = object_size;
  pool->pool_size = pool_size;
  pool->allocated_count = 0;
  pool->free_count = pool_size;

  // Initialize free list - use another page for free list
  pool->free_list = (void **)kalloc();
  if (!pool->free_list) {
    object_pool_destroy(pool);
    return 0;
  }

  // Setup free list pointers with bounds checking
  char *obj_ptr = (char *)pool->memory_base;
  uint max_free_entries = 4096 / sizeof(void *);
  uint entries_to_create =
      (pool_size < max_free_entries) ? pool_size : max_free_entries;

  for (uint i = 0; i < entries_to_create; i++) {
    pool->free_list[i] = obj_ptr;
    obj_ptr += object_size;
  }

  return pool;
}

void *object_pool_alloc(struct simple_object_pool *pool) {
  if (!pool || pool->free_count == 0 || pool->free_count > pool->pool_size)
    return 0;

  pool->free_count--;
  void *obj = pool->free_list[pool->free_count];
  pool->allocated_count++;

  return obj;
}

void object_pool_free(struct simple_object_pool *pool, void *obj) {
  if (!pool || !obj || pool->free_count >= pool->pool_size) return;

  pool->free_list[pool->free_count] = obj;
  pool->free_count++;
  pool->allocated_count--;
}

void object_pool_destroy(struct simple_object_pool *pool) {
  if (!pool) return;

  if (pool->memory_base) kfree(pool->memory_base);
  if (pool->free_list) kfree(pool->free_list);
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

  // Test object pool
  struct simple_object_pool *pool =
      object_pool_create(MEDIUM_OBJ_SIZE, POOL_SIZE);
  if (!pool) {
    printf("Failed to create object pool\n");
    return 0;
  }

  start_time = read_time();
  for (int i = 0; i < BENCHMARK_ITERATIONS && i < POOL_SIZE; i++) {
    void *obj = object_pool_alloc(pool);
    if (obj) {
      *(uint32 *)obj = i;  // Simulate work
      object_pool_free(pool, obj);
    }
  }
  end_time = read_time();

  int pool_ops =
      (BENCHMARK_ITERATIONS < POOL_SIZE) ? BENCHMARK_ITERATIONS : POOL_SIZE;
  calculate_performance_metrics(start_time, end_time, pool_ops * 2,
                                pool_ops * MEDIUM_OBJ_SIZE,
                                POOL_SIZE * sizeof(void *), &pool_metrics);

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

  // Test pool latency
  struct simple_object_pool *pool =
      object_pool_create(MEDIUM_OBJ_SIZE, POOL_SIZE);
  if (!pool) {
    printf("Failed to create object pool\n");
    kfree(slab_latencies);
    kfree(pool_latencies);
    return 0;
  }

  for (int i = 0; i < samples && i < POOL_SIZE; i++) {
    uint64 start = read_time();
    void *obj = object_pool_alloc(pool);
    uint64 end = read_time();
    pool_latencies[i] = end - start;

    if (obj) object_pool_free(pool, obj);
  }

  object_pool_destroy(pool);

  // Calculate statistics
  uint64 slab_min = ~0ULL, slab_max = 0, slab_total = 0;
  uint64 pool_min = ~0ULL, pool_max = 0, pool_total = 0;

  for (int i = 0; i < samples; i++) {
    if (slab_latencies[i] < slab_min) slab_min = slab_latencies[i];
    if (slab_latencies[i] > slab_max) slab_max = slab_latencies[i];
    slab_total += slab_latencies[i];

    if (i < POOL_SIZE) {
      if (pool_latencies[i] < pool_min) pool_min = pool_latencies[i];
      if (pool_latencies[i] > pool_max) pool_max = pool_latencies[i];
      pool_total += pool_latencies[i];
    }
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

  // Test pool memory efficiency
  struct simple_object_pool *pool =
      object_pool_create(MEDIUM_OBJ_SIZE, POOL_SIZE);
  if (!pool) {
    printf("Failed to create object pool\n");
    return 0;
  }

  void *pool_objects[100];
  int pool_allocated = 0;

  for (int i = 0; i < test_objects && i < POOL_SIZE; i++) {
    pool_objects[i] = object_pool_alloc(pool);
    if (pool_objects[i]) pool_allocated++;
  }

  uint64 pool_payload = pool_allocated * MEDIUM_OBJ_SIZE;
  uint64 pool_overhead =
      POOL_SIZE * sizeof(void *) + sizeof(struct simple_object_pool);

  // Clean up pool objects
  for (int i = 0; i < pool_allocated; i++) {
    if (pool_objects[i]) object_pool_free(pool, pool_objects[i]);
  }
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

  // Pool mixed workload
  struct simple_object_pool *pool =
      object_pool_create(MEDIUM_OBJ_SIZE, POOL_SIZE);
  if (!pool) {
    printf("Failed to create object pool\n");
    return 0;
  }

  void *pool_objects[100];
  start_time = read_time();
  int pool_ops = 0;

  for (int round = 0; round < 10; round++) {  // Reduced iterations
    // Allocate batch
    for (int i = 0; i < 100 && pool->free_count > 0; i++) {
      pool_objects[i] = object_pool_alloc(pool);
      if (pool_objects[i]) {
        *(uint32 *)pool_objects[i] = round * 100 + i;
        pool_ops++;
      }
    }

    // Free half
    for (int i = 0; i < 50; i++) {
      if (pool_objects[i]) {
        object_pool_free(pool, pool_objects[i]);
        pool_objects[i] = 0;
        pool_ops++;
      }
    }

    // Allocate more
    for (int i = 0; i < 50 && pool->free_count > 0; i++) {
      if (!pool_objects[i]) {
        pool_objects[i] = object_pool_alloc(pool);
        if (pool_objects[i]) pool_ops++;
      }
    }

    // Free all
    for (int i = 0; i < 100; i++) {
      if (pool_objects[i]) {
        object_pool_free(pool, pool_objects[i]);
        pool_objects[i] = 0;
        pool_ops++;
      }
    }
  }

  end_time = read_time();
  calculate_performance_metrics(start_time, end_time, pool_ops,
                                pool_ops * MEDIUM_OBJ_SIZE / 4,
                                POOL_SIZE * sizeof(void *), &pool_metrics);

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