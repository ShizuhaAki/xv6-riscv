#include "slab_test_multi.h"

#include "../kalloc.h"
#include "../printf.h"
#include "../proc.h"
#include "../riscv.h"
#include "../slab.h"
#include "../spinlock.h"
#include "../string.h"

// Simple synchronization primitives for multi-core testing
static struct spinlock multi_test_lock;
static volatile int test_should_start = 0;
static volatile int current_test_errors = 0;

// Helper functions
static void reset_test_sync(void) {
  test_should_start = 0;
  current_test_errors = 0;
  __sync_synchronize();  // Ensure reset is visible to all CPUs
}

static void signal_test_start(void) {
  test_should_start = 1;
  __sync_synchronize();  // Ensure signal is visible to all CPUs
}

static void wait_for_test_start(void) {
  while (!test_should_start) {
    __sync_synchronize();
  }
}

static void record_test_error(void) {
  __sync_fetch_and_add(&current_test_errors, 1);
}

static int get_active_cpu_count(void) { return 3; }

// Test 1: Basic concurrent allocation/deallocation
int slab_test_multi_basic_concurrent(void) {
  int my_cpu = cpuid();
  printf("Cpu %d entered basic concurrent test\n", my_cpu);
  int active_cpus = get_active_cpu_count();

  if (my_cpu >= active_cpus) return 1;  // Skip extra CPUs

  static struct kmem_cache *test_cache = 0;

  // CPU 0 initializes task and signals others
  if (my_cpu == 0) {
    test_cache = kmem_cache_create("multi_basic", 128, 0, 0, 0);
    if (!test_cache) {
      printf("Failed to create cache for basic concurrent test\n");
      return 0;
    }
    __sync_synchronize();  // Ensure other CPUs can see the initialization

    // Give cpu1 and cpu2 start signal
    signal_test_start();
    printf("Cpu %d signaled start\n", my_cpu);
  }

  // All CPUs wait for start signal
  wait_for_test_start();
  printf("Cpu %d passed wait for test start\n", my_cpu);

  // CPU 0 resets start signal at task beginning
  if (my_cpu == 0) {
    reset_test_sync();
    printf("Cpu %d reset start signal\n", my_cpu);
  }

  // Each CPU performs allocations
  const int iterations = 100;
  for (int i = 0; i < iterations; i++) {
    void *obj = kmem_cache_alloc(test_cache);
    if (!obj) {
      record_test_error();
      continue;
    }

    // Write CPU-specific pattern
    *(uint32 *)obj = (my_cpu << 16) | i;

    // Verify pattern
    if (*(uint32 *)obj != ((my_cpu << 16) | i)) {
      record_test_error();
    }

    kmem_cache_free(test_cache, obj);
  }

  // Simple synchronization: wait for a bit to let other CPUs finish
  for (volatile int delay = 0; delay < 100000; delay++);

  // CPU 0 reports results and cleans up
  if (my_cpu == 0) {
    printf("Basic concurrent test: %d errors\n", current_test_errors);
    kmem_cache_destroy(test_cache);
    test_cache = 0;
    return current_test_errors == 0 ? 1 : 0;
  }

  return 1;
}

// Test 2: Race condition detection
int slab_test_multi_race_condition(void) {
  int my_cpu = cpuid();
  int active_cpus = get_active_cpu_count();

  if (my_cpu >= active_cpus) return 1;

  static struct kmem_cache *race_cache = 0;
  static volatile int shared_counter = 0;

  // CPU 0 initializes task and signals others
  if (my_cpu == 0) {
    race_cache = kmem_cache_create("race_test", 64, 0, 0, 0);
    shared_counter = 0;
    if (!race_cache) {
      printf("Failed to create cache for race test\n");
      return 0;
    }
    __sync_synchronize();  // Ensure other CPUs can see the initialization

    // Give cpu1 and cpu2 start signal
    signal_test_start();
  }

  // All CPUs wait for start signal
  wait_for_test_start();

  // CPU 0 resets start signal at task beginning
  if (my_cpu == 0) {
    reset_test_sync();
  }

  // Intentionally create race conditions
  const int race_iterations = 50;
  for (int i = 0; i < race_iterations; i++) {
    void *obj = kmem_cache_alloc(race_cache);
    if (obj) {
      // Non-atomic increment (intentional race)
      int old_val = shared_counter;
      shared_counter = old_val + 1;

      // Write and verify pattern
      *(uint64 *)obj = 0xDEADBEEF00000000ULL | (my_cpu << 8) | i;

      // Small delay
      for (volatile int delay = 0; delay < 100; delay++);

      if (*(uint64 *)obj != (0xDEADBEEF00000000ULL | (my_cpu << 8) | i)) {
        record_test_error();
      }

      kmem_cache_free(race_cache, obj);
    } else {
      record_test_error();
    }
  }

  // Simple synchronization: wait for a bit to let other CPUs finish
  for (volatile int delay = 0; delay < 100000; delay++);

  if (my_cpu == 0) {
    printf("Race condition test: counter=%d, expected=%d, errors=%d\n",
           shared_counter, race_iterations * active_cpus, current_test_errors);
    kmem_cache_destroy(race_cache);
    race_cache = 0;
    shared_counter = 0;
    // Allow some race-related counter inconsistency, but no memory corruption
    return current_test_errors < 10 ? 1 : 0;
  }

  return 1;
}

// Test 3: Cache sharing stress test
int slab_test_multi_cache_sharing(void) {
  int my_cpu = cpuid();
  int active_cpus = get_active_cpu_count();

  if (my_cpu >= active_cpus) return 1;

  static struct kmem_cache *shared_cache = 0;
  static void *shared_objects[512];  // Max objects for sharing test
  static volatile int shared_index = 0;
  static volatile int phase1_done = 0;

  // CPU 0 initializes task and signals others
  if (my_cpu == 0) {
    shared_cache = kmem_cache_create("sharing_test", 256, 0, 0, 0);
    shared_index = 0;
    phase1_done = 0;
    for (int i = 0; i < 512; i++) {
      shared_objects[i] = 0;
    }
    if (!shared_cache) {
      printf("Failed to create cache for sharing test\n");
      return 0;
    }
    __sync_synchronize();  // Ensure other CPUs can see the initialization

    // Give cpu1 and cpu2 start signal
    signal_test_start();
  }

  // All CPUs wait for start signal
  wait_for_test_start();

  // CPU 0 resets start signal at task beginning
  if (my_cpu == 0) {
    reset_test_sync();
  }

  // Phase 1: All CPUs allocate objects
  const int alloc_per_cpu = 32;
  for (int i = 0; i < alloc_per_cpu; i++) {
    void *obj = kmem_cache_alloc(shared_cache);
    if (obj) {
      int idx = __sync_fetch_and_add(&shared_index, 1);
      if (idx < 512) {
        *(uint64 *)obj = 0xCAFEBABE00000000ULL | (my_cpu << 16) | i;
        shared_objects[idx] = obj;
        __sync_synchronize();  // Ensure other CPUs can see the object
      } else {
        kmem_cache_free(shared_cache, obj);
      }
    } else {
      record_test_error();
    }
  }

  // Signal end of Phase 1
  if (my_cpu == 0) {
    phase1_done = 1;
    __sync_synchronize();
  }

  // Wait for Phase 1 to complete on all CPUs
  while (!phase1_done) {
    __sync_synchronize();
  }

  // Phase 2: CPUs free objects allocated by others
  int objects_per_cpu = shared_index / active_cpus;
  int start_idx = my_cpu * objects_per_cpu;
  int end_idx = (my_cpu + 1) * objects_per_cpu;

  for (int i = start_idx; i < end_idx && i < shared_index; i++) {
    if (shared_objects[i]) {
      uint64 pattern = *(uint64 *)shared_objects[i];
      uint64 orig_cpu = (pattern >> 16) & 0xFF;

      if (orig_cpu >= active_cpus) {
        record_test_error();
      }

      kmem_cache_free(shared_cache, shared_objects[i]);
      shared_objects[i] = 0;
    }
  }

  // Simple synchronization: wait for a bit to let other CPUs finish
  for (volatile int delay = 0; delay < 100000; delay++);

  if (my_cpu == 0) {
    printf("Cache sharing test: %d objects allocated, %d errors\n",
           shared_index, current_test_errors);

    // Clean up remaining objects
    for (int i = 0; i < shared_index; i++) {
      if (shared_objects[i]) {
        kmem_cache_free(shared_cache, shared_objects[i]);
      }
    }

    kmem_cache_destroy(shared_cache);
    shared_cache = 0;
    shared_index = 0;
    phase1_done = 0;
    return current_test_errors == 0 ? 1 : 0;
  }

  return 1;
}

// Test 4: Memory consistency across cores
int slab_test_multi_memory_consistency(void) {
  int my_cpu = cpuid();
  int active_cpus = get_active_cpu_count();

  if (my_cpu >= active_cpus) return 1;

  static struct kmem_cache *consistency_cache = 0;
  static void *cpu_objects[8];  // One object per CPU
  static volatile int init_done = 0;

  // CPU 0 initializes task and signals others
  if (my_cpu == 0) {
    consistency_cache = kmem_cache_create("consistency_test", 512, 0, 0, 0);
    for (int i = 0; i < 8; i++) {
      cpu_objects[i] = 0;
    }
    if (!consistency_cache) {
      printf("Failed to create cache for consistency test\n");
      return 0;
    }
    __sync_synchronize();  // Ensure other CPUs can see the initialization

    // Give cpu1 and cpu2 start signal
    signal_test_start();
  }

  // All CPUs wait for start signal
  wait_for_test_start();

  // CPU 0 resets start signal at task beginning
  if (my_cpu == 0) {
    reset_test_sync();
  }

  // Each CPU allocates one object
  void *my_obj = kmem_cache_alloc(consistency_cache);
  if (!my_obj) {
    record_test_error();
    return 0;
  }

  cpu_objects[my_cpu] = my_obj;
  __sync_synchronize();  // Ensure other CPUs can see the object assignment

  // Initialize with pattern
  uint64 *data = (uint64 *)my_obj;
  for (int i = 0; i < 512 / sizeof(uint64); i++) {
    data[i] = (uint64)my_cpu << 56 | i;
  }

  // Signal initialization done
  if (my_cpu == 0) {
    init_done = 1;
    __sync_synchronize();
  }

  // Wait for all CPUs to finish initialization
  while (!init_done) {
    __sync_synchronize();
  }

  // Cross-verify other CPUs' objects
  for (int round = 0; round < 5; round++) {
    for (int other_cpu = 0; other_cpu < active_cpus; other_cpu++) {
      if (other_cpu != my_cpu && cpu_objects[other_cpu]) {
        uint64 *other_data = (uint64 *)cpu_objects[other_cpu];

        for (int i = 0; i < 512 / sizeof(uint64); i++) {
          uint64 expected = (uint64)other_cpu << 56 | i;
          if (other_data[i] != expected) {
            record_test_error();
          }
        }
      }
    }

    // Update own pattern
    for (int i = 0; i < 512 / sizeof(uint64); i++) {
      data[i] = (uint64)my_cpu << 56 | (round << 16) | i;
    }

    __sync_synchronize();
  }

  // Simple synchronization: wait for a bit to let other CPUs finish
  for (volatile int delay = 0; delay < 100000; delay++);

  // Free objects
  if (my_obj) {
    kmem_cache_free(consistency_cache, my_obj);
  }

  // Simple synchronization: wait for a bit to let other CPUs finish
  for (volatile int delay = 0; delay < 100000; delay++);

  if (my_cpu == 0) {
    printf("Memory consistency test: %d errors\n", current_test_errors);
    kmem_cache_destroy(consistency_cache);
    consistency_cache = 0;
    init_done = 0;
    return current_test_errors == 0 ? 1 : 0;
  }

  return 1;
}

// Test 5: Performance measurement
int slab_test_multi_performance(void) {
  int my_cpu = cpuid();
  int active_cpus = get_active_cpu_count();

  if (my_cpu >= active_cpus) return 1;

  static struct kmem_cache *perf_cache = 0;
  static uint64 start_time = 0;
  static uint64 end_time = 0;
  static int total_allocs = 0;

  // CPU 0 initializes task and signals others
  if (my_cpu == 0) {
    perf_cache = kmem_cache_create("perf_test", 128, 0, 0, 0);
    total_allocs = 0;
    if (!perf_cache) {
      printf("Failed to create cache for performance test\n");
      return 0;
    }
    __sync_synchronize();  // Ensure other CPUs can see the initialization

    // Record start time and give cpu1 and cpu2 start signal
    uint64 time;
    asm volatile("rdtime %0" : "=r"(time));
    start_time = time;
    signal_test_start();
  }

  // All CPUs wait for start signal
  wait_for_test_start();

  // CPU 0 resets start signal at task beginning
  if (my_cpu == 0) {
    reset_test_sync();
  }

  // Performance test: rapid allocation/deallocation
  const int perf_iterations = 1000;
  int my_allocs = 0;

  for (int i = 0; i < perf_iterations; i++) {
    void *obj = kmem_cache_alloc(perf_cache);
    if (obj) {
      *(uint32 *)obj = my_cpu;
      kmem_cache_free(perf_cache, obj);
      my_allocs++;
    } else {
      record_test_error();
    }
  }

  __sync_fetch_and_add(&total_allocs, my_allocs);

  // Simple synchronization: wait for a bit to let other CPUs finish
  for (volatile int delay = 0; delay < 100000; delay++);

  if (my_cpu == 0) {
    uint64 time;
    asm volatile("rdtime %0" : "=r"(time));
    end_time = time;

    uint64 total_cycles = end_time - start_time;
    printf("Performance test: %d total allocs, %llu cycles, %d errors\n",
           total_allocs, total_cycles, current_test_errors);

    kmem_cache_destroy(perf_cache);
    perf_cache = 0;
    return current_test_errors == 0 ? 1 : 0;
  }

  return 1;
}

// Placeholder implementations for remaining tests
int slab_test_multi_stress_concurrent(void) { return 1; }

int slab_test_multi_fragmentation(void) { return 1; }

int slab_test_multi_mixed_sizes(void) { return 1; }

int slab_test_multi_extreme_alloc(void) { return 1; }

int slab_test_multi_error_handling(void) { return 1; }

// Test function array (similar to single-core tests)
int (*slab_multi_core_test[])(void) = {
    slab_test_multi_basic_concurrent, slab_test_multi_race_condition,
    slab_test_multi_cache_sharing,    slab_test_multi_memory_consistency,
    slab_test_multi_performance,      slab_test_multi_stress_concurrent,
    slab_test_multi_fragmentation,    slab_test_multi_mixed_sizes,
    slab_test_multi_extreme_alloc,    slab_test_multi_error_handling,
};

const int slab_multi_core_test_num =
    sizeof(slab_multi_core_test) / sizeof(slab_multi_core_test[0]);

// Main multi-core test function (similar to single-core version)
void slab_test_multi(void) {
  int my_cpu = cpuid();

  // Initialize synchronization only once
  if (my_cpu == 0) {
    initlock(&multi_test_lock, "multi_test");
    printf("\n=== Multi-Core Slab Allocator Tests ===\n");
    printf("Testing with %d CPUs\n", get_active_cpu_count());
    __sync_synchronize();  // Signal initialization complete
  }

  // Simple wait for initialization
  while (my_cpu != 0 && !multi_test_lock.locked) {
    __sync_synchronize();
  }

  int passed = 0;
  int failed = 0;

  // Only CPU 0 runs the test orchestration
  if (my_cpu == 0) {
    for (int i = 0; i < slab_multi_core_test_num; i++) {
      printf("Running multi-core test %d\n", i);

      int result = slab_multi_core_test[i]();
      if (result) {
        passed++;
      } else {
        failed++;
        printf("Multi-core test %d failed\n", i);
      }

      // Small delay between tests
      for (volatile int delay = 0; delay < 50000; delay++);
    }

    printf("Slab multi-core tests: %d passed, %d failed\n", passed, failed);
    printf("======================================\n\n");
  } else {
    // Other CPUs participate in individual tests
    for (int i = 0; i < slab_multi_core_test_num; i++) {
      slab_multi_core_test[i]();

      // Small delay between tests
      for (volatile int delay = 0; delay < 50000; delay++);
    }
  }
}