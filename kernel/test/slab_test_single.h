#pragma once

// Single-threaded slab test entry function
void slab_test_single(void);

// Individual single-threaded test functions
int slab_test_single_basic_alloc(void);
int slab_test_single_batch_alloc(void);
int slab_test_single_unaligned_batch(void);
int slab_test_single_large_batch(void);
int slab_test_single_huge_batch(void);
int slab_test_single_random_free(void);
int slab_test_single_ctor_dtor(void);
int slab_test_single_memory_integrity(void);
int slab_test_single_edge_cases(void);
int slab_test_single_reuse_cycles(void);
int slab_test_single_alignment(void);
int slab_test_single_stress(void);
int slab_test_single_cache_destroy(void);
int slab_test_single_error_handling(void);
int slab_test_single_multi_cache(void);
int slab_test_single_extreme_alloc(void);
int slab_test_single_corruption_detection(void);
int slab_test_single_fragmentation(void);
int slab_test_single_boundary_conditions(void);
