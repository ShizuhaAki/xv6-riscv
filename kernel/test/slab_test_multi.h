#pragma once

#include "../spinlock.h"
#include "../types.h"

// Test configuration constants
#define MAX_TEST_CPUS 8
#define TEST_ITERATIONS 1000
#define TEST_OBJECTS_PER_CPU 64
#define TEST_CACHE_SIZES 4

// Test synchronization structures
struct cpu_test_data {
  int cpu_id;
  int completed;
  int errors;
  uint64 start_time;
  uint64 end_time;
  void* allocated_objects[TEST_OBJECTS_PER_CPU];
  int alloc_count;
};

struct multi_test_context {
  struct spinlock sync_lock;
  volatile int started_cpus;
  volatile int ready_cpus;
  volatile int finished_cpus;
  volatile int should_start;
  volatile int should_stop;
  struct cpu_test_data cpu_data[MAX_TEST_CPUS];
  int total_errors;
  char test_name[64];
};

// Multi-threaded slab test entry function
void slab_test_multi(void);

// Individual multi-core test functions
int slab_test_multi_basic_concurrent(void);
int slab_test_multi_race_condition(void);
int slab_test_multi_cache_sharing(void);
int slab_test_multi_stress_concurrent(void);
int slab_test_multi_fragmentation(void);
int slab_test_multi_mixed_sizes(void);
int slab_test_multi_barrier_sync(void);
int slab_test_multi_deadlock_test(void);
int slab_test_multi_memory_consistency(void);
int slab_test_multi_performance(void);

// Helper functions for multi-core testing
void init_test_context(struct multi_test_context* ctx, const char* name);
void cpu_barrier(struct multi_test_context* ctx);
void cpu_sync_start(struct multi_test_context* ctx);
int get_participating_cpu_count(void);
uint64 get_cycle_count(void);
void record_error(struct multi_test_context* ctx, int cpu_id,
                  const char* error_msg);
