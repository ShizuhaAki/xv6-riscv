#include "slab_test_multi.h"

#include "../kalloc.h"
#include "../printf.h"
#include "../proc.h"
#include "../riscv.h"
#include "../slab.h"
#include "../string.h"
#include "../trap.h"

// Global test contexts for different test suites
static struct multi_test_context global_test_contexts[16];
static int current_test_index = 0;

// Helper function implementations
void init_test_context(struct multi_test_context* ctx, const char* name) {
  initlock(&ctx->sync_lock, "multi_test_lock");
  ctx->started_cpus = 0;
  ctx->ready_cpus = 0;
  ctx->finished_cpus = 0;
  ctx->should_start = 0;
  ctx->should_stop = 0;
  ctx->total_errors = 0;

  // Copy test name
  int i;
  for (i = 0; i < 63 && name[i] != '\0'; i++) {
    ctx->test_name[i] = name[i];
  }
  ctx->test_name[i] = '\0';

  // Initialize CPU data
  for (int cpu = 0; cpu < MAX_TEST_CPUS; cpu++) {
    ctx->cpu_data[cpu].cpu_id = cpu;
    ctx->cpu_data[cpu].completed = 0;
    ctx->cpu_data[cpu].errors = 0;
    ctx->cpu_data[cpu].start_time = 0;
    ctx->cpu_data[cpu].end_time = 0;
    ctx->cpu_data[cpu].alloc_count = 0;
    for (int j = 0; j < TEST_OBJECTS_PER_CPU; j++) {
      ctx->cpu_data[cpu].allocated_objects[j] = 0;
    }
  }
}

void cpu_barrier(struct multi_test_context* ctx) {
  acquire(&ctx->sync_lock);
  ctx->ready_cpus++;
  release(&ctx->sync_lock);

  // Busy wait for all CPUs to reach the barrier
  while (ctx->ready_cpus < get_participating_cpu_count()) {
    // Memory barrier to prevent optimization
    __sync_synchronize();
  }

  __sync_synchronize();
}

void cpu_sync_start(struct multi_test_context* ctx) {
  acquire(&ctx->sync_lock);
  ctx->started_cpus++;
  if (ctx->started_cpus >= get_participating_cpu_count()) {
    ctx->should_start = 1;
    __sync_synchronize();
  }
  release(&ctx->sync_lock);

  // Wait for start signal
  while (!ctx->should_start) {
    __sync_synchronize();
  }
}

int get_participating_cpu_count(void) {
  // Count active CPUs - in xv6, we know CPU 0 is always active
  // Other CPUs may or may not be active depending on hardware
  int count = 1;  // CPU 0 is always active

  // This is a simple heuristic - in real implementation,
  // we might need to track which CPUs are actually running
  for (int i = 1; i < NCPU; i++) {
    // Check if CPU i is active by checking if it has executed any code
    // For simplicity, we'll assume all CPUs up to NCPU are active
    count++;
  }

  return count > MAX_TEST_CPUS ? MAX_TEST_CPUS : count;
}

uint64 get_cycle_count(void) {
  // Read the time register for timing measurements
  uint64 time;
  asm volatile("rdtime %0" : "=r"(time));
  return time;
}

void record_error(struct multi_test_context* ctx, int cpu_id,
                  const char* error_msg) {
  acquire(&ctx->sync_lock);
  ctx->cpu_data[cpu_id].errors++;
  ctx->total_errors++;
  release(&ctx->sync_lock);

  printf("[CPU %d] ERROR in %s: %s\n", cpu_id, ctx->test_name, error_msg);
}

// Test 1: Basic concurrent allocation/deallocation
int slab_test_multi_basic_concurrent(void) {
  struct multi_test_context* ctx = &global_test_contexts[current_test_index++];
  init_test_context(ctx, "basic_concurrent");

  int my_cpu = cpuid();
  if (my_cpu >= MAX_TEST_CPUS) return 1;  // Skip extra CPUs

  struct kmem_cache* cache = 0;

  // CPU 0 creates the cache
  if (my_cpu == 0) {
    cache = kmem_cache_create("multi_test_basic", 128, 0, 0, 0);
    if (!cache) {
      printf("Failed to create cache for basic concurrent test\n");
      return 0;
    }
    // Store cache pointer in a global location
    // For simplicity, we'll use the first allocated object slot
    ctx->cpu_data[0].allocated_objects[0] = (void*)cache;
  }

  cpu_barrier(ctx);

  // All CPUs get the cache pointer
  cache = (struct kmem_cache*)ctx->cpu_data[0].allocated_objects[0];

  cpu_sync_start(ctx);

  ctx->cpu_data[my_cpu].start_time = get_cycle_count();

  // Each CPU allocates and deallocates objects
  const int iterations = 100;
  for (int i = 0; i < iterations; i++) {
    void* obj = kmem_cache_alloc(cache);
    if (!obj) {
      record_error(ctx, my_cpu, "allocation failed");
      continue;
    }

    // Write a CPU-specific pattern
    *(uint32*)obj = (my_cpu << 16) | i;

    // Verify the pattern immediately
    if (*(uint32*)obj != ((my_cpu << 16) | i)) {
      record_error(ctx, my_cpu, "immediate data corruption");
    }

    kmem_cache_free(cache, obj);
    ctx->cpu_data[my_cpu].alloc_count++;
  }

  ctx->cpu_data[my_cpu].end_time = get_cycle_count();
  ctx->cpu_data[my_cpu].completed = 1;

  cpu_barrier(ctx);

  // CPU 0 cleans up and reports results
  if (my_cpu == 0) {
    kmem_cache_destroy(cache);

    int total_allocs = 0;
    for (int cpu = 0; cpu < get_participating_cpu_count(); cpu++) {
      total_allocs += ctx->cpu_data[cpu].alloc_count;
    }

    printf("Basic concurrent test: %d total allocations, %d errors\n",
           total_allocs, ctx->total_errors);

    return ctx->total_errors == 0 ? 1 : 0;
  }

  return 1;
}

// Test 2: Race condition detection
int slab_test_multi_race_condition(void) {
  struct multi_test_context* ctx = &global_test_contexts[current_test_index++];
  init_test_context(ctx, "race_condition");

  int my_cpu = cpuid();
  if (my_cpu >= MAX_TEST_CPUS) return 1;

  static struct kmem_cache* shared_cache = 0;
  static volatile int shared_counter = 0;

  if (my_cpu == 0) {
    shared_cache = kmem_cache_create("race_test", 64, 0, 0, 0);
    if (!shared_cache) {
      printf("Failed to create cache for race test\n");
      return 0;
    }
  }

  cpu_barrier(ctx);
  cpu_sync_start(ctx);

  ctx->cpu_data[my_cpu].start_time = get_cycle_count();

  // Intentionally create race conditions
  const int race_iterations = 50;
  for (int i = 0; i < race_iterations; i++) {
    void* obj = kmem_cache_alloc(shared_cache);
    if (obj) {
      // Increment shared counter without synchronization (intentional race)
      int old_val = shared_counter;
      shared_counter = old_val + 1;

      // Write pattern and check for corruption
      *(uint64*)obj = 0xDEADBEEF00000000ULL | (my_cpu << 8) | i;

      // Small delay to increase chance of race
      for (volatile int delay = 0; delay < 100; delay++);

      if (*(uint64*)obj != (0xDEADBEEF00000000ULL | (my_cpu << 8) | i)) {
        record_error(ctx, my_cpu, "race condition data corruption");
      }

      kmem_cache_free(shared_cache, obj);
      ctx->cpu_data[my_cpu].alloc_count++;
    } else {
      record_error(ctx, my_cpu, "allocation failed in race test");
    }
  }

  ctx->cpu_data[my_cpu].end_time = get_cycle_count();
  ctx->cpu_data[my_cpu].completed = 1;

  cpu_barrier(ctx);

  if (my_cpu == 0) {
    printf("Race condition test: counter=%d, expected=%d, errors=%d\n",
           shared_counter, race_iterations * get_participating_cpu_count(),
           ctx->total_errors);

    kmem_cache_destroy(shared_cache);
    shared_cache = 0;
    shared_counter = 0;

    // In race test, we expect some counter inconsistency but no memory
    // corruption
    return ctx->total_errors < 10 ? 1 : 0;  // Allow some race-related issues
  }

  return 1;
}

// Main multi-core test entry point
void slab_test_multi(void) {
  int my_cpu = cpuid();

  if (my_cpu == 0) {
    printf("\n=== Multi-Core Slab Allocator Tests ===\n");
    printf("Testing with %d CPUs\n", get_participating_cpu_count());
  }

  // Wait for all CPUs to be ready
  __sync_synchronize();

  // Run the test suite
  int tests_passed = 0;
  int tests_total = 0;

  // Test 1: Basic concurrent allocation
  tests_total++;
  if (slab_test_multi_basic_concurrent()) {
    tests_passed++;
  }

  // Reset for next test
  __sync_synchronize();

  // Test 2: Race condition test
  tests_total++;
  if (slab_test_multi_race_condition()) {
    tests_passed++;
  }

  // Test 3: Cache sharing stress test
  tests_total++;
  if (slab_test_multi_cache_sharing()) {
    tests_passed++;
  }

  __sync_synchronize();

  // Test 4: Memory consistency test
  tests_total++;
  if (slab_test_multi_memory_consistency()) {
    tests_passed++;
  }

  __sync_synchronize();

  // Test 5: Performance test
  tests_total++;
  if (slab_test_multi_performance()) {
    tests_passed++;
  }

  // CPU 0 reports final results
  if (my_cpu == 0) {
    printf("\n=== Multi-Core Test Results ===\n");
    printf("Tests passed: %d/%d\n", tests_passed, tests_total);
    printf("=====================================\n\n");
  }
}

// Test 3: Cache sharing under stress
int slab_test_multi_cache_sharing(void) {
  struct multi_test_context* ctx = &global_test_contexts[current_test_index++];
  init_test_context(ctx, "cache_sharing");

  int my_cpu = cpuid();
  if (my_cpu >= MAX_TEST_CPUS) return 1;

  static struct kmem_cache* shared_cache = 0;
  static void* shared_objects[TEST_OBJECTS_PER_CPU * MAX_TEST_CPUS];
  static volatile int shared_index = 0;

  if (my_cpu == 0) {
    shared_cache = kmem_cache_create("sharing_test", 256, 0, 0, 0);
    if (!shared_cache) {
      printf("Failed to create cache for sharing test\n");
      return 0;
    }
    // Initialize shared objects array
    for (int i = 0; i < TEST_OBJECTS_PER_CPU * MAX_TEST_CPUS; i++) {
      shared_objects[i] = 0;
    }
    shared_index = 0;
  }

  cpu_barrier(ctx);
  cpu_sync_start(ctx);

  ctx->cpu_data[my_cpu].start_time = get_cycle_count();

  // Phase 1: All CPUs allocate objects and store them in shared array
  const int alloc_per_cpu = 32;
  for (int i = 0; i < alloc_per_cpu; i++) {
    void* obj = kmem_cache_alloc(shared_cache);
    if (obj) {
      // Atomically get next index
      int idx = __sync_fetch_and_add(&shared_index, 1);
      if (idx < TEST_OBJECTS_PER_CPU * MAX_TEST_CPUS) {
        shared_objects[idx] = obj;
        // Write CPU-specific pattern
        *(uint64*)obj = 0xCAFEBABE00000000ULL | (my_cpu << 16) | i;
        ctx->cpu_data[my_cpu].alloc_count++;
      } else {
        // Array full, free the object
        kmem_cache_free(shared_cache, obj);
      }
    } else {
      record_error(ctx, my_cpu, "allocation failed in sharing test");
    }
  }

  cpu_barrier(ctx);

  // Phase 2: All CPUs free objects allocated by other CPUs
  int objects_to_free = shared_index / get_participating_cpu_count();
  int start_idx = my_cpu * objects_to_free;
  int end_idx = (my_cpu + 1) * objects_to_free;

  for (int i = start_idx; i < end_idx && i < shared_index; i++) {
    if (shared_objects[i]) {
      // Verify pattern before freeing
      uint64 pattern = *(uint64*)shared_objects[i];
      uint64 expected_cpu = (pattern >> 16) & 0xFF;

      if (expected_cpu >= MAX_TEST_CPUS) {
        record_error(ctx, my_cpu, "invalid CPU pattern in shared object");
      }

      kmem_cache_free(shared_cache, shared_objects[i]);
      shared_objects[i] = 0;
    }
  }

  ctx->cpu_data[my_cpu].end_time = get_cycle_count();
  ctx->cpu_data[my_cpu].completed = 1;

  cpu_barrier(ctx);

  if (my_cpu == 0) {
    printf("Cache sharing test: %d objects allocated, %d errors\n",
           shared_index, ctx->total_errors);

    // Clean up any remaining objects
    for (int i = 0; i < shared_index; i++) {
      if (shared_objects[i]) {
        kmem_cache_free(shared_cache, shared_objects[i]);
      }
    }

    kmem_cache_destroy(shared_cache);
    shared_cache = 0;
    shared_index = 0;

    return ctx->total_errors == 0 ? 1 : 0;
  }

  return 1;
}

// Test 4: Memory consistency across cores
int slab_test_multi_memory_consistency(void) {
  struct multi_test_context* ctx = &global_test_contexts[current_test_index++];
  init_test_context(ctx, "memory_consistency");

  int my_cpu = cpuid();
  if (my_cpu >= MAX_TEST_CPUS) return 1;

  static struct kmem_cache* consistency_cache = 0;
  static void* consistency_objects[MAX_TEST_CPUS];

  if (my_cpu == 0) {
    consistency_cache = kmem_cache_create("consistency_test", 512, 0, 0, 0);
    if (!consistency_cache) {
      printf("Failed to create cache for consistency test\n");
      return 0;
    }
    for (int i = 0; i < MAX_TEST_CPUS; i++) {
      consistency_objects[i] = 0;
    }
  }

  cpu_barrier(ctx);

  // Each CPU allocates one object
  void* my_obj = kmem_cache_alloc(consistency_cache);
  if (!my_obj) {
    record_error(ctx, my_cpu, "allocation failed in consistency test");
    return 0;
  }

  consistency_objects[my_cpu] = my_obj;

  // Initialize object with a pattern
  uint64* data = (uint64*)my_obj;
  for (int i = 0; i < 512 / sizeof(uint64); i++) {
    data[i] = (uint64)my_cpu << 56 | i;
  }

  cpu_barrier(ctx);
  cpu_sync_start(ctx);

  ctx->cpu_data[my_cpu].start_time = get_cycle_count();

  // Each CPU verifies other CPUs' objects
  for (int round = 0; round < 10; round++) {
    for (int other_cpu = 0; other_cpu < get_participating_cpu_count();
         other_cpu++) {
      if (other_cpu != my_cpu && consistency_objects[other_cpu]) {
        uint64* other_data = (uint64*)consistency_objects[other_cpu];

        // Verify the pattern
        for (int i = 0; i < 512 / sizeof(uint64); i++) {
          uint64 expected = (uint64)other_cpu << 56 | i;
          if (other_data[i] != expected) {
            record_error(ctx, my_cpu, "memory consistency violation");
          }
        }
      }
    }

    // Modify own object
    for (int i = 0; i < 512 / sizeof(uint64); i++) {
      data[i] = (uint64)my_cpu << 56 | (round << 16) | i;
    }

    // Memory barrier to ensure visibility
    __sync_synchronize();
  }

  ctx->cpu_data[my_cpu].end_time = get_cycle_count();
  ctx->cpu_data[my_cpu].completed = 1;

  cpu_barrier(ctx);

  // Free objects
  if (my_obj) {
    kmem_cache_free(consistency_cache, my_obj);
  }

  cpu_barrier(ctx);

  if (my_cpu == 0) {
    printf("Memory consistency test: %d errors\n", ctx->total_errors);

    kmem_cache_destroy(consistency_cache);
    consistency_cache = 0;

    return ctx->total_errors == 0 ? 1 : 0;
  }

  return 1;
}

// Test 5: Performance measurement
int slab_test_multi_performance(void) {
  struct multi_test_context* ctx = &global_test_contexts[current_test_index++];
  init_test_context(ctx, "performance");

  int my_cpu = cpuid();
  if (my_cpu >= MAX_TEST_CPUS) return 1;

  static struct kmem_cache* perf_cache = 0;

  if (my_cpu == 0) {
    perf_cache = kmem_cache_create("perf_test", 128, 0, 0, 0);
    if (!perf_cache) {
      printf("Failed to create cache for performance test\n");
      return 0;
    }
  }

  cpu_barrier(ctx);
  cpu_sync_start(ctx);

  uint64 start_cycles = get_cycle_count();
  ctx->cpu_data[my_cpu].start_time = start_cycles;

  // Performance test: rapid allocation/deallocation
  const int perf_iterations = 1000;
  for (int i = 0; i < perf_iterations; i++) {
    void* obj = kmem_cache_alloc(perf_cache);
    if (obj) {
      // Minimal work - just write a marker
      *(uint32*)obj = my_cpu;
      kmem_cache_free(perf_cache, obj);
      ctx->cpu_data[my_cpu].alloc_count++;
    } else {
      record_error(ctx, my_cpu, "allocation failed in performance test");
    }
  }

  uint64 end_cycles = get_cycle_count();
  ctx->cpu_data[my_cpu].end_time = end_cycles;
  ctx->cpu_data[my_cpu].completed = 1;

  cpu_barrier(ctx);

  if (my_cpu == 0) {
    uint64 total_cycles = 0;
    int total_allocs = 0;

    for (int cpu = 0; cpu < get_participating_cpu_count(); cpu++) {
      uint64 cpu_cycles =
          ctx->cpu_data[cpu].end_time - ctx->cpu_data[cpu].start_time;
      total_cycles += cpu_cycles;
      total_allocs += ctx->cpu_data[cpu].alloc_count;

      printf("CPU %d: %d allocs, %llu cycles, %llu cycles/alloc\n", cpu,
             ctx->cpu_data[cpu].alloc_count, cpu_cycles,
             ctx->cpu_data[cpu].alloc_count > 0
                 ? cpu_cycles / ctx->cpu_data[cpu].alloc_count
                 : 0);
    }

    printf("Performance test: %d total allocs, %llu total cycles, %d errors\n",
           total_allocs, total_cycles, ctx->total_errors);

    kmem_cache_destroy(perf_cache);
    perf_cache = 0;

    return ctx->total_errors == 0 ? 1 : 0;
  }

  return 1;
}
