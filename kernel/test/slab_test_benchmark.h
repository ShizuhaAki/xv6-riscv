#pragma once

#include "../types.h"

// Performance metrics structure
struct perf_metrics {
  uint64 total_cycles;
  uint64 total_operations;
  uint64 avg_latency_cycles;
  uint64 min_latency_cycles;
  uint64 max_latency_cycles;
  uint64 throughput_ops_per_sec;
  uint64 memory_allocated_bytes;
  uint64 memory_overhead_bytes;
  uint64 memory_efficiency_percent_x100;  // efficiency * 100 as integer
};

// Object pool structure for comparison
struct simple_object_pool {
  void *memory_base;
  uint object_size;
  uint pool_size;
  uint allocated_count;
  void **free_list;
  uint free_count;
};

// Benchmark test entry function
void slab_test_benchmark(void);

// Core comparison functions
int benchmark_compare_throughput(void);
int benchmark_compare_latency(void);
int benchmark_compare_memory_efficiency(void);
int benchmark_compare_mixed_workload(void);

// Object pool functions
struct simple_object_pool *object_pool_create(uint object_size, uint pool_size);
void *object_pool_alloc(struct simple_object_pool *pool);
void object_pool_free(struct simple_object_pool *pool, void *obj);
void object_pool_destroy(struct simple_object_pool *pool);

// Utility functions
void print_performance_metrics(const char *test_name,
                               struct perf_metrics *metrics);
void calculate_performance_metrics(uint64 start_time, uint64 end_time,
                                   int operations, uint64 memory_used,
                                   uint64 memory_overhead,
                                   struct perf_metrics *metrics);
