#pragma once

// Khugepaged and huge page test functions

// Initialize and start the test runner as a kthread
void khugepaged_test_init(void);

// Run all khugepaged tests
void khugepaged_test_all(void);

// Individual test functions
int khugepaged_test_superalloc_basic(void);
int khugepaged_test_superalloc_multiple(void);
int khugepaged_test_superfree_realloc(void);
int khugepaged_test_superpage_memory(void);
int khugepaged_test_walk_superpage(void);
int khugepaged_test_map_superpage(void);
int khugepaged_test_is_superpage(void);
int khugepaged_test_size_constants(void);
int khugepaged_test_daemon_running(void);
int khugepaged_test_demote_superpage(void);
int khugepaged_test_alloc_exhaustion(void);
int khugepaged_test_pte_flags(void);
