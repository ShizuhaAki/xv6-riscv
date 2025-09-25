#pragma once

// Multi-threaded slab test entry function
void slab_test_multi(void);

// Individual multi-core test functions
int slab_test_multi_basic_concurrent(void);
int slab_test_multi_race_condition(void);
int slab_test_multi_cache_sharing(void);
int slab_test_multi_memory_consistency(void);
int slab_test_multi_performance(void);
int slab_test_multi_stress_concurrent(void);
int slab_test_multi_fragmentation(void);
int slab_test_multi_mixed_sizes(void);
int slab_test_multi_extreme_alloc(void);
int slab_test_multi_error_handling(void);
