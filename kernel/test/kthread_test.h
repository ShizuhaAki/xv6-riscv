#pragma once

// Kernel thread test functions

// Initialize and start the test runner as a kthread
void kthread_test_init(void);

// Run all kthread tests
void kthread_test_all(void);

// Individual test functions
int kthread_test_basic_create(void);
int kthread_test_arg_passing(void);
int kthread_test_multiple_create(void);
int kthread_test_sleep_wakeup(void);
int kthread_test_state_verification(void);
int kthread_test_naming(void);
int kthread_test_kernel_stack(void);
int kthread_test_yield_behavior(void);
int kthread_test_stress_many_threads(void);
int kthread_test_func_pointer(void);
