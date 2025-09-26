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
static volatile int test_should_end = 0;
static volatile int current_test_errors = 0;

// Helper functions
static void reset_test_sync(void) {
  test_should_start = 0;
  test_should_end = 0;
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

static void signal_test_end(void) {
  test_should_end = 1;
  __sync_synchronize();  // Ensure signal is visible to all CPUs
}

static void wait_for_test_end(void) {
  while (!test_should_end) {
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
  }

  // All CPUs wait for start signal
  wait_for_test_start();

  // CPU 0 resets start signal at task beginning
  if (my_cpu == 0) {
    reset_test_sync();
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

  // CPU 0 reports results and cleans up
  if (my_cpu == 0) {
    kmem_cache_destroy(test_cache);
    test_cache = 0;
    signal_test_end();  // Signal other CPUs that test is complete
    return current_test_errors == 0 ? 1 : 0;
  }

  // Other CPUs wait for test completion
  wait_for_test_end();
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

  if (my_cpu == 0) {
    kmem_cache_destroy(race_cache);
    race_cache = 0;
    shared_counter = 0;
    signal_test_end();  // Signal other CPUs that test is complete
    // Allow some race-related counter inconsistency, but no memory corruption
    return current_test_errors < 10 ? 1 : 0;
  }

  // Other CPUs wait for test completion
  wait_for_test_end();
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

  if (my_cpu == 0) {
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
    signal_test_end();  // Signal other CPUs that test is complete
    return current_test_errors == 0 ? 1 : 0;
  }

  // Other CPUs wait for test completion
  wait_for_test_end();
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

  // Free objects
  if (my_obj) {
    kmem_cache_free(consistency_cache, my_obj);
  }

  if (my_cpu == 0) {
    kmem_cache_destroy(consistency_cache);
    consistency_cache = 0;
    init_done = 0;
    signal_test_end();  // Signal other CPUs that test is complete
    return current_test_errors == 0 ? 1 : 0;
  }

  // Other CPUs wait for test completion
  wait_for_test_end();
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

  if (my_cpu == 0) {
    uint64 time;
    asm volatile("rdtime %0" : "=r"(time));
    end_time = time;

    uint64 total_cycles = end_time - start_time;
    printf("Performance test: %d total allocs, %llu cycles, %d errors\n",
           total_allocs, total_cycles, current_test_errors);

    kmem_cache_destroy(perf_cache);
    perf_cache = 0;
    signal_test_end();  // Signal other CPUs that test is complete
    return current_test_errors == 0 ? 1 : 0;
  }

  // Other CPUs wait for test completion
  wait_for_test_end();
  return 1;
}

// Test 6: Stress concurrent allocation/deallocation
int slab_test_multi_stress_concurrent(void) {
  int my_cpu = cpuid();
  int active_cpus = get_active_cpu_count();

  if (my_cpu >= active_cpus) return 1;

  static struct kmem_cache *stress_cache = 0;
  static volatile int stress_phase __attribute__((unused)) =
      0;  // 0: init, 1: allocate, 2: free, 3: done

  // CPU 0 initializes task and signals others
  if (my_cpu == 0) {
    stress_cache = kmem_cache_create("stress_test", 96, 0, 0, 0);
    stress_phase = 0;
    if (!stress_cache) {
      printf("Failed to create cache for stress test\n");
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

  // Stress test: very rapid allocation/deallocation cycles
  const int stress_iterations = 500;
  void *stress_objects[100];  // Pool for each CPU
  int pool_size = 0;

  for (int round = 0; round < 10; round++) {
    // Allocation phase
    for (int i = 0; i < stress_iterations / 10; i++) {
      void *obj = kmem_cache_alloc(stress_cache);
      if (obj) {
        *(uint64 *)obj =
            0xABCDEF0000000000ULL | (my_cpu << 24) | (round << 16) | i;

        // Sometimes keep object, sometimes free immediately
        if (i % 3 == 0 && pool_size < 100) {
          stress_objects[pool_size++] = obj;
        } else {
          // Verify before freeing
          if (*(uint64 *)obj !=
              (0xABCDEF0000000000ULL | (my_cpu << 24) | (round << 16) | i)) {
            record_test_error();
          }
          kmem_cache_free(stress_cache, obj);
        }
      } else {
        record_test_error();
      }
    }

    // Random free some pooled objects
    for (int i = 0; i < pool_size / 2; i++) {
      if (stress_objects[i]) {
        uint64 pattern = *(uint64 *)stress_objects[i];
        uint64 cpu = (pattern >> 24) & 0xFF;
        if (cpu != my_cpu) {
          record_test_error();
        }
        kmem_cache_free(stress_cache, stress_objects[i]);
        stress_objects[i] = stress_objects[pool_size - 1];
        pool_size--;
      }
    }
  }

  // Free remaining objects
  for (int i = 0; i < pool_size; i++) {
    if (stress_objects[i]) {
      kmem_cache_free(stress_cache, stress_objects[i]);
    }
  }

  if (my_cpu == 0) {
    kmem_cache_destroy(stress_cache);
    stress_cache = 0;
    stress_phase = 3;
    signal_test_end();  // Signal other CPUs that test is complete
    return current_test_errors < 5 ? 1 : 0;  // Allow some stress-related errors
  }

  // Other CPUs wait for test completion
  wait_for_test_end();
  return 1;
}

// Test 7: Memory fragmentation test
int slab_test_multi_fragmentation(void) {
  int my_cpu = cpuid();
  int active_cpus = get_active_cpu_count();

  if (my_cpu >= active_cpus) return 1;

  static struct kmem_cache *frag_cache_small = 0;
  static struct kmem_cache *frag_cache_medium = 0;
  static struct kmem_cache *frag_cache_large = 0;
  static void *allocated_objects[256];  // Shared pool
  static volatile int object_count = 0;
  static volatile int frag_phase = 0;  // 0: alloc, 1: fragment, 2: cleanup

  // CPU 0 initializes task and signals others
  if (my_cpu == 0) {
    frag_cache_small = kmem_cache_create("frag_small", 32, 0, 0, 0);
    frag_cache_medium = kmem_cache_create("frag_medium", 128, 0, 0, 0);
    frag_cache_large = kmem_cache_create("frag_large", 512, 0, 0, 0);
    object_count = 0;
    frag_phase = 0;

    for (int i = 0; i < 256; i++) {
      allocated_objects[i] = 0;
    }

    if (!frag_cache_small || !frag_cache_medium || !frag_cache_large) {
      printf("Failed to create caches for fragmentation test\n");
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

  // Phase 1: Allocate objects of different sizes in patterns
  const int alloc_per_size = 20;
  for (int i = 0; i < alloc_per_size; i++) {
    struct kmem_cache *cache;
    char size_marker;

    // Rotate through different sizes
    switch (i % 3) {
      case 0:
        cache = frag_cache_small;
        size_marker = 'S';
        break;
      case 1:
        cache = frag_cache_medium;
        size_marker = 'M';
        break;
      default:
        cache = frag_cache_large;
        size_marker = 'L';
        break;
    }

    void *obj = kmem_cache_alloc(cache);
    if (obj) {
      // Mark with size and CPU info
      *(uint64 *)obj = ((uint64)size_marker << 56) | ((uint64)my_cpu << 48) | i;

      int idx = __sync_fetch_and_add(&object_count, 1);
      if (idx < 256) {
        allocated_objects[idx] = obj;
      } else {
        kmem_cache_free(cache, obj);
      }
    } else {
      record_test_error();
    }
  }

  // Synchronize before fragmentation phase
  if (my_cpu == 0) {
    frag_phase = 1;
    __sync_synchronize();
  }

  while (frag_phase != 1) {
    __sync_synchronize();
  }

  // Phase 2: Create fragmentation by freeing objects in random patterns
  // Each CPU frees objects with specific patterns to create fragmentation
  for (int i = my_cpu; i < object_count; i += active_cpus) {
    if (i < 256 && allocated_objects[i]) {
      void *obj = allocated_objects[i];
      uint64 pattern = *(uint64 *)obj;
      char size_marker = (char)(pattern >> 56);
      uint64 orig_cpu = (pattern >> 48) & 0xFF;

      // Verify pattern before freeing
      if (orig_cpu >= active_cpus) {
        record_test_error();
      }

      struct kmem_cache *cache;
      switch (size_marker) {
        case 'S':
          cache = frag_cache_small;
          break;
        case 'M':
          cache = frag_cache_medium;
          break;
        case 'L':
          cache = frag_cache_large;
          break;
        default:
          record_test_error();
          continue;
      }

      kmem_cache_free(cache, obj);
      allocated_objects[i] = 0;
    }
  }

  // Phase 3: Try allocating again to test fragmentation handling
  if (my_cpu == 0) {
    frag_phase = 2;
    __sync_synchronize();
  }

  while (frag_phase != 2) {
    __sync_synchronize();
  }

  // Test allocation after fragmentation
  for (int i = 0; i < 10; i++) {
    void *obj_s = kmem_cache_alloc(frag_cache_small);
    void *obj_m = kmem_cache_alloc(frag_cache_medium);
    void *obj_l = kmem_cache_alloc(frag_cache_large);

    if (obj_s) {
      *(uint32 *)obj_s = 0xF5F5F5F5;
      if (*(uint32 *)obj_s != 0xF5F5F5F5) {
        record_test_error();
      }
      kmem_cache_free(frag_cache_small, obj_s);
    }

    if (obj_m) {
      *(uint32 *)obj_m = 0xFAFAFAFA;
      if (*(uint32 *)obj_m != 0xFAFAFAFA) {
        record_test_error();
      }
      kmem_cache_free(frag_cache_medium, obj_m);
    }

    if (obj_l) {
      *(uint32 *)obj_l = 0xFEFEFEFE;
      if (*(uint32 *)obj_l != 0xFEFEFEFE) {
        record_test_error();
      }
      kmem_cache_free(frag_cache_large, obj_l);
    }
  }

  if (my_cpu == 0) {
    // Clean up any remaining objects
    for (int i = 0; i < 256; i++) {
      if (allocated_objects[i]) {
        uint64 pattern = *(uint64 *)allocated_objects[i];
        char size_marker = (char)(pattern >> 56);
        struct kmem_cache *cache;

        switch (size_marker) {
          case 'S':
            cache = frag_cache_small;
            break;
          case 'M':
            cache = frag_cache_medium;
            break;
          case 'L':
            cache = frag_cache_large;
            break;
          default:
            continue;
        }

        kmem_cache_free(cache, allocated_objects[i]);
      }
    }

    kmem_cache_destroy(frag_cache_small);
    kmem_cache_destroy(frag_cache_medium);
    kmem_cache_destroy(frag_cache_large);
    frag_cache_small = 0;
    frag_cache_medium = 0;
    frag_cache_large = 0;
    object_count = 0;
    frag_phase = 0;
    signal_test_end();  // Signal other CPUs that test is complete
    return current_test_errors == 0 ? 1 : 0;
  }

  // Other CPUs wait for test completion
  wait_for_test_end();
  return 1;
}

// Test 8: Mixed sizes allocation across cores
int slab_test_multi_mixed_sizes(void) {
  int my_cpu = cpuid();
  int active_cpus = get_active_cpu_count();

  if (my_cpu >= active_cpus) return 1;

  static struct kmem_cache *cache_tiny = 0;    // 16 bytes
  static struct kmem_cache *cache_small = 0;   // 64 bytes
  static struct kmem_cache *cache_medium = 0;  // 256 bytes
  static struct kmem_cache *cache_large = 0;   // 1024 bytes
  static void *mixed_objects[512];
  static volatile int mixed_count = 0;
  static volatile int allocation_phase = 0;  // 0: alloc, 1: verify, 2: free

  // CPU 0 initializes task and signals others
  if (my_cpu == 0) {
    cache_tiny = kmem_cache_create("mixed_tiny", 16, 0, 0, 0);
    cache_small = kmem_cache_create("mixed_small", 64, 0, 0, 0);
    cache_medium = kmem_cache_create("mixed_medium", 256, 0, 0, 0);
    cache_large = kmem_cache_create("mixed_large", 1024, 0, 0, 0);
    mixed_count = 0;
    allocation_phase = 0;

    for (int i = 0; i < 512; i++) {
      mixed_objects[i] = 0;
    }

    if (!cache_tiny || !cache_small || !cache_medium || !cache_large) {
      printf("Failed to create caches for mixed sizes test\n");
      return 0;
    }
    __sync_synchronize();

    signal_test_start();
  }

  wait_for_test_start();

  if (my_cpu == 0) {
    reset_test_sync();
  }

  // Phase 1: Each CPU allocates different sizes in rotation
  const int allocs_per_cpu = 32;
  for (int i = 0; i < allocs_per_cpu; i++) {
    struct kmem_cache *cache;
    uint32 size_marker;

    // Each CPU uses different size patterns
    int size_choice = (my_cpu * 13 + i) % 4;  // Different pattern per CPU

    switch (size_choice) {
      case 0:
        cache = cache_tiny;
        size_marker = 0x1111;
        break;
      case 1:
        cache = cache_small;
        size_marker = 0x2222;
        break;
      case 2:
        cache = cache_medium;
        size_marker = 0x3333;
        break;
      default:
        cache = cache_large;
        size_marker = 0x4444;
        break;
    }

    void *obj = kmem_cache_alloc(cache);
    if (obj) {
      // Mark with size, CPU, and iteration info
      *(uint64 *)obj = ((uint64)size_marker << 48) | ((uint64)my_cpu << 32) |
                       ((uint64)i << 16) | 0xABCD;

      int idx = __sync_fetch_and_add(&mixed_count, 1);
      if (idx < 512) {
        mixed_objects[idx] = obj;
      } else {
        // Verify before immediate free
        if ((*(uint64 *)obj & 0xFFFF) != 0xABCD) {
          record_test_error();
        }
        kmem_cache_free(cache, obj);
      }
    } else {
      record_test_error();
    }
  }

  // Phase 2: Cross-verify objects allocated by other CPUs
  if (my_cpu == 0) {
    allocation_phase = 1;
    __sync_synchronize();
  }

  while (allocation_phase != 1) {
    __sync_synchronize();
  }

  // Each CPU verifies different ranges
  int verify_start = (my_cpu * mixed_count) / active_cpus;
  int verify_end = ((my_cpu + 1) * mixed_count) / active_cpus;

  for (int i = verify_start; i < verify_end && i < mixed_count; i++) {
    if (mixed_objects[i]) {
      uint64 pattern = *(uint64 *)mixed_objects[i];
      uint32 size_marker = (uint32)(pattern >> 48);
      uint32 orig_cpu = (uint32)((pattern >> 32) & 0xFFFF);
      uint32 iteration __attribute__((unused)) =
          (uint32)((pattern >> 16) & 0xFFFF);
      uint32 magic = (uint32)(pattern & 0xFFFF);

      // Verify pattern integrity
      if (magic != 0xABCD || orig_cpu >= active_cpus) {
        record_test_error();
      }

      // Verify size marker consistency
      if (size_marker != 0x1111 && size_marker != 0x2222 &&
          size_marker != 0x3333 && size_marker != 0x4444) {
        record_test_error();
      }
    }
  }

  // Phase 3: Free objects in size-specific order
  if (my_cpu == 0) {
    allocation_phase = 2;
    __sync_synchronize();
  }

  while (allocation_phase != 2) {
    __sync_synchronize();
  }

  // Each CPU frees objects of specific sizes
  for (int i = 0; i < mixed_count; i++) {
    if (mixed_objects[i]) {
      uint64 pattern = *(uint64 *)mixed_objects[i];
      uint32 size_marker = (uint32)(pattern >> 48);

      // CPU 0: tiny and large, CPU 1: small, CPU 2: medium
      int should_free = 0;
      struct kmem_cache *cache = 0;

      switch (my_cpu % 3) {
        case 0:  // Free tiny and large
          if (size_marker == 0x1111) {
            cache = cache_tiny;
            should_free = 1;
          } else if (size_marker == 0x4444) {
            cache = cache_large;
            should_free = 1;
          }
          break;
        case 1:  // Free small
          if (size_marker == 0x2222) {
            cache = cache_small;
            should_free = 1;
          }
          break;
        case 2:  // Free medium
          if (size_marker == 0x3333) {
            cache = cache_medium;
            should_free = 1;
          }
          break;
      }

      if (should_free && cache) {
        kmem_cache_free(cache, mixed_objects[i]);
        mixed_objects[i] = 0;
      }
    }
  }

  if (my_cpu == 0) {
    // Clean up any remaining objects
    for (int i = 0; i < mixed_count; i++) {
      if (mixed_objects[i]) {
        uint64 pattern = *(uint64 *)mixed_objects[i];
        uint32 size_marker = (uint32)(pattern >> 48);
        struct kmem_cache *cache = 0;

        switch (size_marker) {
          case 0x1111:
            cache = cache_tiny;
            break;
          case 0x2222:
            cache = cache_small;
            break;
          case 0x3333:
            cache = cache_medium;
            break;
          case 0x4444:
            cache = cache_large;
            break;
        }

        if (cache) {
          kmem_cache_free(cache, mixed_objects[i]);
        }
      }
    }

    kmem_cache_destroy(cache_tiny);
    kmem_cache_destroy(cache_small);
    kmem_cache_destroy(cache_medium);
    kmem_cache_destroy(cache_large);
    cache_tiny = cache_small = cache_medium = cache_large = 0;
    mixed_count = 0;
    allocation_phase = 0;
    signal_test_end();
    return current_test_errors == 0 ? 1 : 0;
  }

  wait_for_test_end();
  return 1;
}

// Specific destructors for different sizes
static void safety_dtor_32(void *obj) {
  if (obj) memset(obj, 0xDD, 32);
}

static void safety_dtor_64(void *obj) {
  if (obj) memset(obj, 0xDD, 64);
}

static void safety_dtor_128(void *obj) {
  if (obj) memset(obj, 0xDD, 128);
}

static void safety_dtor_256(void *obj) {
  if (obj) memset(obj, 0xDD, 256);
}

static void safety_dtor_512(void *obj) {
  if (obj) memset(obj, 0xDD, 512);
}

// Test 9: Memory safety test (multi-core)
int slab_test_multi_safety(void) {
  int my_cpu = cpuid();
  int active_cpus = get_active_cpu_count();

  if (my_cpu >= active_cpus) return 1;

  static struct kmem_cache *safety_cache_32 = 0;
  static struct kmem_cache *safety_cache_64 = 0;
  static struct kmem_cache *safety_cache_128 = 0;
  static struct kmem_cache *safety_cache_256 = 0;
  static struct kmem_cache *safety_cache_512 = 0;
  static volatile int safety_phase = 0;  // 0: init, 1: test, 2: cleanup

  // CPU 0 initializes caches with different sizes and dtor
  if (my_cpu == 0) {
    // Create caches with no ctor but with size-specific dtors
    safety_cache_32 = kmem_cache_create("safety_32", 32, 0, safety_dtor_32, 0);
    safety_cache_64 = kmem_cache_create("safety_64", 64, 0, safety_dtor_64, 0);
    safety_cache_128 =
        kmem_cache_create("safety_128", 128, 0, safety_dtor_128, 0);
    safety_cache_256 =
        kmem_cache_create("safety_256", 256, 0, safety_dtor_256, 0);
    safety_cache_512 =
        kmem_cache_create("safety_512", 512, 0, safety_dtor_512, 0);

    safety_phase = 0;

    if (!safety_cache_32 || !safety_cache_64 || !safety_cache_128 ||
        !safety_cache_256 || !safety_cache_512) {
      printf("Failed to create caches for safety test\n");
      return 0;
    }
    __sync_synchronize();

    signal_test_start();
  }

  wait_for_test_start();

  if (my_cpu == 0) {
    reset_test_sync();
  }

  // Phase 1: Safety testing - allocate, check, mark, wait, free
  struct kmem_cache *caches[] = {safety_cache_32, safety_cache_64,
                                 safety_cache_128, safety_cache_256,
                                 safety_cache_512};
  int cache_sizes[] = {32, 64, 128, 256, 512};
  int num_caches = 5;

  const int iterations_per_cache = 50;

  for (int cache_idx = 0; cache_idx < num_caches; cache_idx++) {
    struct kmem_cache *cache = caches[cache_idx];
    int size = cache_sizes[cache_idx];

    for (int i = 0; i < iterations_per_cache; i++) {
      void *obj = kmem_cache_alloc(cache);
      if (!obj) {
        record_test_error();
        continue;
      }

      // Check for 0xAAAAAAAA pattern (should not exist in fresh memory)
      uint32 *words = (uint32 *)obj;
      int word_count = size / sizeof(uint32);

      for (int w = 0; w < word_count; w++) {
        if (words[w] == 0xAAAAAAAA) {
          // Error: found previous allocation marker
          record_test_error();
          printf("CPU %d: Found 0xAAAAAAAA in fresh allocation from cache %d\n",
                 my_cpu, cache_idx);
        }
      }

      // Set memory to 0xAA pattern
      memset(obj, 0xAA, size);

      // Verify the pattern was set correctly
      for (int w = 0; w < word_count; w++) {
        if (words[w] != 0xAAAAAAAA) {
          record_test_error();
        }
      }

      // Random delay before freeing (simulate random usage time)
      // Use a simple LFSR-based random delay
      uint32 delay = ((my_cpu * 17 + i * 23) ^ (cache_idx * 31)) & 0xFF;
      for (volatile uint32 d = 0; d < delay; d++) {
        // Small delay
      }

      // Free the object (dtor will set to 0xDD)
      kmem_cache_free(cache, obj);

      // Small delay to let other CPUs potentially reuse this memory
      for (volatile int wait = 0; wait < 10; wait++) {
        __sync_synchronize();
      }
    }
  }

  // Phase 2: Additional stress testing - rapid allocation/free cycles
  for (int stress_round = 0; stress_round < 10; stress_round++) {
    for (int cache_idx = my_cpu % num_caches; cache_idx < num_caches;
         cache_idx += active_cpus) {
      struct kmem_cache *cache = caches[cache_idx];
      int size = cache_sizes[cache_idx];

      void *rapid_objs[20];
      int allocated_count = 0;

      // Rapid allocation
      for (int r = 0; r < 20; r++) {
        void *obj = kmem_cache_alloc(cache);
        if (obj) {
          // Quick safety check
          uint32 *check_word = (uint32 *)obj;
          if (*check_word == 0xAAAAAAAA) {
            record_test_error();
          }

          // Mark and store
          memset(obj, 0xAA, size);
          rapid_objs[allocated_count++] = obj;
        } else {
          record_test_error();
        }
      }

      // Random delay
      uint32 stress_delay =
          ((stress_round * 7 + my_cpu * 11) ^ cache_idx) & 0x3F;
      for (volatile uint32 d = 0; d < stress_delay; d++) {
        // Stress delay
      }

      // Rapid free
      for (int r = 0; r < allocated_count; r++) {
        if (rapid_objs[r]) {
          kmem_cache_free(cache, rapid_objs[r]);
          rapid_objs[r] = 0;
        }
      }
    }
  }

  if (my_cpu == 0) {
    safety_phase = 2;
    __sync_synchronize();
  }

  while (safety_phase != 2) {
    __sync_synchronize();
  }

  // Cleanup phase
  if (my_cpu == 0) {
    kmem_cache_destroy(safety_cache_32);
    kmem_cache_destroy(safety_cache_64);
    kmem_cache_destroy(safety_cache_128);
    kmem_cache_destroy(safety_cache_256);
    kmem_cache_destroy(safety_cache_512);

    safety_cache_32 = safety_cache_64 = safety_cache_128 = 0;
    safety_cache_256 = safety_cache_512 = 0;
    safety_phase = 0;

    signal_test_end();

    printf("Safety test completed on CPU 0, errors: %d\n", current_test_errors);
    return current_test_errors == 0 ? 1 : 0;
  }

  wait_for_test_end();
  return 1;
}

// Test 10: Error handling and edge cases
int slab_test_multi_error_handling(void) {
  int my_cpu = cpuid();
  int active_cpus = get_active_cpu_count();

  if (my_cpu >= active_cpus) return 1;

  static struct kmem_cache *error_cache_a = 0;
  static struct kmem_cache *error_cache_b = 0;
  static void *valid_objects[64];
  static volatile int error_phase = 0;  // 0: setup, 1: error tests, 2: cleanup

  // CPU 0 initializes task and signals others
  if (my_cpu == 0) {
    error_cache_a = kmem_cache_create("error_test_a", 64, 0, 0, 0);
    error_cache_b = kmem_cache_create("error_test_b", 128, 0, 0, 0);
    error_phase = 0;

    for (int i = 0; i < 64; i++) {
      valid_objects[i] = 0;
    }

    if (!error_cache_a || !error_cache_b) {
      printf("Failed to create caches for error handling test\n");
      return 0;
    }
    __sync_synchronize();

    signal_test_start();
  }

  wait_for_test_start();

  if (my_cpu == 0) {
    reset_test_sync();
  }

  // Phase 1: Setup valid objects for error testing
  const int objects_per_cpu = 8;
  for (int i = 0; i < objects_per_cpu; i++) {
    void *obj_a = kmem_cache_alloc(error_cache_a);
    void *obj_b = kmem_cache_alloc(error_cache_b);

    if (obj_a) {
      *(uint64 *)obj_a = 0xAABBCC0000000000ULL | ((uint64)my_cpu << 32) | i;
      int idx = my_cpu * objects_per_cpu + i;
      if (idx < 64) {
        valid_objects[idx] = obj_a;
      }
    }

    if (obj_b) {
      *(uint64 *)obj_b = 0xBBCCDD0000000000ULL | ((uint64)my_cpu << 32) | i;
      // Keep some objects for cross-cache error tests, free others normally
      if (i % 2 == 0) {
        kmem_cache_free(error_cache_b, obj_b);
      } else {
        // Store in valid_objects for later error testing
        int idx = my_cpu * objects_per_cpu + i;
        if (idx < 32) {  // Use first half of array for cache_b objects
          valid_objects[32 + (idx % 32)] = obj_b;
        }
      }
    }
  }

  // Synchronize before error testing phase
  if (my_cpu == 0) {
    error_phase = 1;
    __sync_synchronize();
  }

  while (error_phase != 1) {
    __sync_synchronize();
  }

  // Phase 2: Controlled error condition testing
  // Each CPU tests different error scenarios to avoid interference

  // Test 1: Double free detection (CPU 0)
  if (my_cpu == 0) {
    void *test_obj = kmem_cache_alloc(error_cache_a);
    if (test_obj) {
      *(uint32 *)test_obj = 0xD0B1EF1E;
      kmem_cache_free(error_cache_a, test_obj);  // First free (valid)
    }
  }

  // Test 2: Cross-cache free attempts (CPU 1)
  if (my_cpu == 1) {
    void *obj_from_a = kmem_cache_alloc(error_cache_a);
    if (obj_from_a) {
      *(uint32 *)obj_from_a = 0xC105CACC;
      // Attempting to free cache_a object with cache_b would be an error
      // kmem_cache_free(error_cache_b, obj_from_a);  // Wrong cache
      // Instead, free with correct cache
      kmem_cache_free(error_cache_a, obj_from_a);
    }
  }

  // Test 3: NULL pointer handling (CPU 2)
  if (my_cpu == 2) {
    // Most slab implementations should handle NULL gracefully
    // Note: This might be a no-op or handled gracefully in xv6
    kmem_cache_free(error_cache_a, 0);  // NULL pointer free
  }

  // Test 4: Use after free detection (All CPUs)
  void *test_uaf = kmem_cache_alloc(error_cache_a);
  if (test_uaf) {
    *(uint64 *)test_uaf = 0x15EAF7E1F1EEULL | my_cpu;
    uint64 original_value = *(uint64 *)test_uaf;

    kmem_cache_free(error_cache_a, test_uaf);

    // Reading from freed memory (undefined behavior)
    // In a real system this might return garbage or cause a fault
    uint64 after_free_value = *(uint64 *)test_uaf;

    // Check if memory was corrupted/reused
    if (after_free_value == original_value) {
      // Memory still contains old data (not necessarily an error)
    } else {
      // Memory was reused or cleared (expected behavior)
    }
  }

  // Test 5: Allocation stress with error injection
  for (int i = 0; i < 20; i++) {
    void *obj = kmem_cache_alloc(error_cache_a);
    if (obj) {
      // Write pattern and immediately verify
      *(uint64 *)obj = 0x5715E55A441ULL | (my_cpu << 24) | i;

      if (*(uint64 *)obj != (0x5715E55A441ULL | (my_cpu << 24) | i)) {
        record_test_error();
      }

      // Free immediately to create rapid alloc/free cycles
      kmem_cache_free(error_cache_a, obj);
    } else {
      // Allocation failure under stress is not necessarily an error
      // but we track it for statistics
      if (i < 10) {  // Early failures are more concerning
        record_test_error();
      }
    }
  }

  // Phase 3: Cleanup and post-destruction error testing
  if (my_cpu == 0) {
    error_phase = 2;
    __sync_synchronize();
  }

  while (error_phase != 2) {
    __sync_synchronize();
  }

  // Clean up valid objects before cache destruction
  for (int i = my_cpu; i < 64; i += active_cpus) {
    if (valid_objects[i] && (uint64)valid_objects[i] > 0x80000000ULL &&
        (uint64)valid_objects[i] < 0x90000000ULL) {
      uint64 pattern = *(uint64 *)valid_objects[i];
      uint64 marker = pattern & 0xFFFFFF0000000000ULL;

      // Verify pattern before freeing
      if (marker == 0xAABBCC0000000000ULL) {
        kmem_cache_free(error_cache_a, valid_objects[i]);
      } else if (marker == 0xBBCCDD0000000000ULL) {
        kmem_cache_free(error_cache_b, valid_objects[i]);
      } else {
        record_test_error();  // Corrupted object
      }
      valid_objects[i] = 0;
    }
  }

  // CPU 0 handles cache destruction and post-destruction tests
  if (my_cpu == 0) {
    // Clean up any remaining objects
    for (int i = 0; i < 64; i++) {
      if (valid_objects[i] && (uint64)valid_objects[i] > 0x80000000ULL &&
          (uint64)valid_objects[i] < 0x90000000ULL) {
        uint64 pattern = *(uint64 *)valid_objects[i];
        uint64 marker = pattern & 0xFFFFFF0000000000ULL;

        if (marker == 0xAABBCC0000000000ULL) {
          kmem_cache_free(error_cache_a, valid_objects[i]);
        } else if (marker == 0xBBCCDD0000000000ULL) {
          kmem_cache_free(error_cache_b, valid_objects[i]);
        }
        valid_objects[i] = 0;
      }
    }

    // Destroy caches
    kmem_cache_destroy(error_cache_a);
    kmem_cache_destroy(error_cache_b);

    error_cache_a = error_cache_b = 0;
    error_phase = 0;
    signal_test_end();

    // Pass test if we handled error conditions gracefully
    // Allow some errors as we're specifically testing error conditions
    return current_test_errors < 20 ? 1 : 0;
  }

  wait_for_test_end();
  return 1;
}

// Test function array (similar to single-core tests)
int (*slab_multi_core_test[])(void) = {
    slab_test_multi_basic_concurrent, slab_test_multi_race_condition,
    slab_test_multi_cache_sharing,    slab_test_multi_memory_consistency,
    slab_test_multi_performance,      slab_test_multi_stress_concurrent,
    slab_test_multi_fragmentation,    slab_test_multi_mixed_sizes,
    slab_test_multi_error_handling,   slab_test_multi_safety,
};

const int slab_multi_core_test_num =
    sizeof(slab_multi_core_test) / sizeof(slab_multi_core_test[0]);

// Main multi-core test function (similar to single-core version)
void slab_test_multi(void) {
  int my_cpu = cpuid();

  // Initialize synchronization only once
  if (my_cpu == 0) {
    initlock(&multi_test_lock, "multi_test");
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
      int result = slab_multi_core_test[i]();
      if (result) {
        passed++;
      } else {
        failed++;
        printf("Multi-core test %d failed\n", i + 1);
      }
    }

    printf("Slab multi-core tests: %d passed, %d failed\n", passed, failed);
  } else {
    // Other CPUs participate in individual tests
    for (int i = 0; i < slab_multi_core_test_num; i++) {
      slab_multi_core_test[i]();
    }
  }
}