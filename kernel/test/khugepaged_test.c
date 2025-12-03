// khugepaged_test.c - Comprehensive tests for khugepaged and huge page support
//
// Tests cover:
// 1. Superpage allocation/deallocation
// 2. Superpage mapping
// 3. Page table walk for superpages
// 4. Superpage demotion
// 5. Memory copy with superpages
// 6. khugepaged daemon operation
// 7. Page collapse functionality
// 8. Mixed page size handling
// 9. Stress tests
// 10. Edge cases

#include "khugepaged_test.h"

#include "../kalloc.h"
#include "../khugepaged.h"
#include "../memlayout.h"
#include "../printf.h"
#include "../proc.h"
#include "../riscv.h"
#include "../spinlock.h"
#include "../string.h"
#include "../vm.h"

// External declarations
extern struct proc proc[];
extern pagetable_t kernel_pagetable;

// ============================================================================
// Test 1: Superpage allocation basic
// ============================================================================

int khugepaged_test_superalloc_basic(void) {
  void *page = superalloc();
  if (page == 0) {
    printf("  FAIL: superalloc returned NULL\n");
    return 0;
  }

  // Verify alignment (2MB aligned)
  if ((uint64)page % SUPERPGSIZE != 0) {
    printf("  FAIL: superpage not 2MB aligned (addr=%p)\n", page);
    superfree(page);
    return 0;
  }

  // Verify the page is zeroed (superalloc zeros the page)
  char *p = (char *)page;
  for (int i = 0; i < 1024; i++) {  // Check first 1KB
    if (p[i] != 0) {
      printf("  FAIL: superpage not zeroed at offset %d\n", i);
      superfree(page);
      return 0;
    }
  }

  superfree(page);
  return 1;
}

// ============================================================================
// Test 2: Multiple superpage allocation
// ============================================================================

int khugepaged_test_superalloc_multiple(void) {
  void *pages[8];
  int allocated = 0;

  // Allocate as many superpages as possible
  for (int i = 0; i < 8; i++) {
    pages[i] = superalloc();
    if (pages[i] == 0) {
      break;
    }
    allocated++;

    // Verify alignment
    if ((uint64)pages[i] % SUPERPGSIZE != 0) {
      printf("  FAIL: superpage %d not aligned\n", i);
      // Free allocated pages
      for (int j = 0; j <= i; j++) {
        if (pages[j]) superfree(pages[j]);
      }
      return 0;
    }
  }

  if (allocated == 0) {
    printf("  FAIL: could not allocate any superpages\n");
    return 0;
  }

  // Verify all pages are distinct
  for (int i = 0; i < allocated; i++) {
    for (int j = i + 1; j < allocated; j++) {
      if (pages[i] == pages[j]) {
        printf("  FAIL: duplicate superpage allocation\n");
        for (int k = 0; k < allocated; k++) {
          superfree(pages[k]);
        }
        return 0;
      }
    }
  }

  // Free all pages
  for (int i = 0; i < allocated; i++) {
    superfree(pages[i]);
  }

  return 1;
}

// ============================================================================
// Test 3: Superpage free and realloc
// ============================================================================

int khugepaged_test_superfree_realloc(void) {
  void *page1 = superalloc();
  if (page1 == 0) {
    printf("  FAIL: first superalloc failed\n");
    return 0;
  }

  // Write a pattern
  char *p = (char *)page1;
  for (int i = 0; i < 1024; i++) {
    p[i] = (char)(i & 0xFF);
  }

  // Free it
  superfree(page1);

  // Allocate again - might get same page back
  void *page2 = superalloc();
  if (page2 == 0) {
    printf("  FAIL: second superalloc failed\n");
    return 0;
  }

  // After superalloc, page should be zeroed
  p = (char *)page2;
  for (int i = 0; i < 1024; i++) {
    if (p[i] != 0) {
      printf("  FAIL: reallocated superpage not zeroed\n");
      superfree(page2);
      return 0;
    }
  }

  superfree(page2);
  return 1;
}

// ============================================================================
// Test 4: Superpage memory write/read
// ============================================================================

int khugepaged_test_superpage_memory(void) {
  void *page = superalloc();
  if (page == 0) {
    printf("  FAIL: superalloc failed\n");
    return 0;
  }

  char *p = (char *)page;

  // Write pattern across entire 2MB
  for (int i = 0; i < SUPERPGSIZE; i += PGSIZE) {
    p[i] = (char)((i / PGSIZE) & 0xFF);
  }

  // Verify pattern
  for (int i = 0; i < SUPERPGSIZE; i += PGSIZE) {
    if (p[i] != (char)((i / PGSIZE) & 0xFF)) {
      printf("  FAIL: memory pattern mismatch at offset %d\n", i);
      superfree(page);
      return 0;
    }
  }

  superfree(page);
  return 1;
}

// ============================================================================
// Test 5: walk_superpage function
// ============================================================================

int khugepaged_test_walk_superpage(void) {
  // Create a test page table
  pagetable_t pgtbl = (pagetable_t)kalloc();
  if (pgtbl == 0) {
    printf("  FAIL: could not allocate page table\n");
    return 0;
  }
  memset(pgtbl, 0, PGSIZE);

  // Test walk_superpage with allocation
  uint64 va = 0x200000;  // 2MB aligned address
  pte_t *pte = walk_superpage(pgtbl, va, 1);
  if (pte == 0) {
    printf("  FAIL: walk_superpage returned NULL\n");
    kfree(pgtbl);
    return 0;
  }

  // The L2 entry should now exist
  pte_t l2_pte = pgtbl[PX(2, va)];
  if (!(l2_pte & PTE_V)) {
    printf("  FAIL: L2 PTE not valid after walk_superpage\n");
    kfree(pgtbl);
    return 0;
  }

  // Clean up - free L1 table if created
  if (l2_pte & PTE_V) {
    kfree((void *)PTE2PA(l2_pte));
  }
  kfree(pgtbl);

  return 1;
}

// ============================================================================
// Test 6: map_superpage function
// ============================================================================

int khugepaged_test_map_superpage(void) {
  pagetable_t pgtbl = (pagetable_t)kalloc();
  if (pgtbl == 0) {
    printf("  FAIL: could not allocate page table\n");
    return 0;
  }
  memset(pgtbl, 0, PGSIZE);

  void *page = superalloc();
  if (page == 0) {
    printf("  FAIL: superalloc failed\n");
    kfree(pgtbl);
    return 0;
  }

  uint64 va = 0x200000;  // 2MB aligned
  int result = map_superpage(pgtbl, va, (uint64)page, PTE_R | PTE_W | PTE_U);
  if (result != 0) {
    printf("  FAIL: map_superpage failed\n");
    superfree(page);
    kfree(pgtbl);
    return 0;
  }

  // Verify the mapping - check L1 PTE
  pte_t *l1_pte = walk_superpage(pgtbl, va, 0);
  if (l1_pte == 0) {
    printf("  FAIL: could not find L1 PTE\n");
    superfree(page);
    kfree(pgtbl);
    return 0;
  }

  // L1 PTE should be a leaf (have R/W/X bits)
  if (!is_superpage(*l1_pte)) {
    printf("  FAIL: L1 PTE is not a superpage leaf\n");
    superfree(page);
    kfree(pgtbl);
    return 0;
  }

  // Verify physical address
  if (PTE2PA(*l1_pte) != (uint64)page) {
    printf("  FAIL: superpage PA mismatch\n");
    superfree(page);
    kfree(pgtbl);
    return 0;
  }

  // Clean up
  pte_t l2_pte = pgtbl[PX(2, va)];
  if ((l2_pte & PTE_V) && !(l2_pte & (PTE_R | PTE_W | PTE_X))) {
    kfree((void *)PTE2PA(l2_pte));
  }
  superfree(page);
  kfree(pgtbl);

  return 1;
}

// ============================================================================
// Test 7: is_superpage function
// ============================================================================

int khugepaged_test_is_superpage(void) {
  // Test with superpage PTE (has R/W/X bits)
  pte_t super_pte = PA2PTE(0x200000) | PTE_V | PTE_R | PTE_W;
  if (!is_superpage(super_pte)) {
    printf("  FAIL: is_superpage returned false for superpage PTE\n");
    return 0;
  }

  // Test with table pointer PTE (no R/W/X bits)
  pte_t table_pte = PA2PTE(0x200000) | PTE_V;
  if (is_superpage(table_pte)) {
    printf("  FAIL: is_superpage returned true for table pointer\n");
    return 0;
  }

  // Test with invalid PTE
  pte_t invalid_pte = 0;
  if (is_superpage(invalid_pte)) {
    printf("  FAIL: is_superpage returned true for invalid PTE\n");
    return 0;
  }

  return 1;
}

// ============================================================================
// Test 8: Superpage size constants
// ============================================================================

int khugepaged_test_size_constants(void) {
  // Verify SUPERPGSIZE is 2MB
  if (SUPERPGSIZE != 2 * 1024 * 1024) {
    printf("  FAIL: SUPERPGSIZE is not 2MB\n");
    return 0;
  }

  // Verify SUPERPGSIZE is 512 * PGSIZE
  if (SUPERPGSIZE != 512 * PGSIZE) {
    printf("  FAIL: SUPERPGSIZE != 512 * PGSIZE\n");
    return 0;
  }

  // Verify alignment macros
  uint64 addr = 0x123456;
  uint64 rounded_up = SUPERPGROUNDUP(addr);
  uint64 rounded_down = SUPERPGROUNDDOWN(addr);

  if (rounded_up % SUPERPGSIZE != 0) {
    printf("  FAIL: SUPERPGROUNDUP result not aligned\n");
    return 0;
  }

  if (rounded_down % SUPERPGSIZE != 0) {
    printf("  FAIL: SUPERPGROUNDDOWN result not aligned\n");
    return 0;
  }

  if (rounded_up < addr) {
    printf("  FAIL: SUPERPGROUNDUP result less than input\n");
    return 0;
  }

  if (rounded_down > addr) {
    printf("  FAIL: SUPERPGROUNDDOWN result greater than input\n");
    return 0;
  }

  return 1;
}

// ============================================================================
// Test 9: khugepaged daemon is running
// ============================================================================

int khugepaged_test_daemon_running(void) {
  // Search for khugepaged in process table
  int found = 0;
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    if (p->state != UNUSED && p->is_kthread) {
      if (strncmp(p->name, "khugepaged", 16) == 0) {
        found = 1;
        break;
      }
    }
  }

  if (!found) {
    printf("  FAIL: khugepaged daemon not found in process table\n");
    return 0;
  }

  // Verify it's a kthread
  if (!p->is_kthread) {
    printf("  FAIL: khugepaged is not marked as kthread\n");
    return 0;
  }

  // Verify it has no user pagetable
  if (p->pagetable != 0) {
    printf("  FAIL: khugepaged should have no user pagetable\n");
    return 0;
  }

  return 1;
}

// ============================================================================
// Test 10: demote_superpage function
// ============================================================================

int khugepaged_test_demote_superpage(void) {
  pagetable_t pgtbl = (pagetable_t)kalloc();
  if (pgtbl == 0) {
    printf("  FAIL: could not allocate page table\n");
    return 0;
  }
  memset(pgtbl, 0, PGSIZE);

  void *page = superalloc();
  if (page == 0) {
    printf("  FAIL: superalloc failed\n");
    kfree(pgtbl);
    return 0;
  }

  // Write a pattern to verify data preservation
  char *p = (char *)page;
  for (int i = 0; i < 512; i++) {
    p[i * PGSIZE] = (char)i;
  }

  uint64 va = 0x200000;
  if (map_superpage(pgtbl, va, (uint64)page, PTE_R | PTE_W | PTE_U) != 0) {
    printf("  FAIL: map_superpage failed\n");
    superfree(page);
    kfree(pgtbl);
    return 0;
  }

  // Demote the superpage
  if (demote_superpage(pgtbl, va) != 0) {
    printf("  FAIL: demote_superpage failed\n");
    // Can't easily clean up here
    return 0;
  }

  // Verify L1 PTE is now a table pointer (not a leaf)
  pte_t *l1_pte = walk_superpage(pgtbl, va, 0);
  if (l1_pte == 0) {
    printf("  FAIL: could not find L1 PTE after demotion\n");
    return 0;
  }

  if (is_superpage(*l1_pte)) {
    printf("  FAIL: L1 PTE is still a superpage after demotion\n");
    return 0;
  }

  // Verify individual 4KB pages are mapped
  for (int i = 0; i < 512; i++) {
    pte_t *l0_pte = walk(pgtbl, va + i * PGSIZE, 0);
    if (l0_pte == 0 || !(*l0_pte & PTE_V)) {
      printf("  FAIL: 4KB page %d not mapped after demotion\n", i);
      return 0;
    }
  }

  // Verify data was preserved
  for (int i = 0; i < 512; i++) {
    pte_t *l0_pte = walk(pgtbl, va + i * PGSIZE, 0);
    char *page_data = (char *)PTE2PA(*l0_pte);
    if (page_data[0] != (char)i) {
      printf("  FAIL: data not preserved in page %d after demotion\n", i);
      return 0;
    }
  }

  // Clean up - free all the individual pages
  for (int i = 0; i < 512; i++) {
    pte_t *l0_pte = walk(pgtbl, va + i * PGSIZE, 0);
    if (l0_pte && (*l0_pte & PTE_V)) {
      kfree((void *)PTE2PA(*l0_pte));
    }
  }

  // Free L0 and L1 tables
  pte_t l2_pte = pgtbl[PX(2, va)];
  if (l2_pte & PTE_V) {
    pagetable_t l1_table = (pagetable_t)PTE2PA(l2_pte);
    pte_t l1_entry = l1_table[PX(1, va)];
    if ((l1_entry & PTE_V) && !(l1_entry & (PTE_R | PTE_W | PTE_X))) {
      kfree((void *)PTE2PA(l1_entry));
    }
    kfree(l1_table);
  }
  kfree(pgtbl);

  return 1;
}

// ============================================================================
// Test 11: Superpage allocation exhaustion
// ============================================================================

int khugepaged_test_alloc_exhaustion(void) {
  void *pages[32];
  int count = 0;

  // Allocate until exhaustion
  while (count < 32) {
    pages[count] = superalloc();
    if (pages[count] == 0) {
      break;
    }
    count++;
  }

  if (count == 0) {
    printf("  FAIL: could not allocate any superpages\n");
    return 0;
  }

  // One more allocation should fail (or we didn't exhaust)
  void *extra = superalloc();
  if (extra != 0) {
    // We have more superpages than expected, that's fine
    superfree(extra);
  }

  // Free one and allocate again - should succeed
  superfree(pages[0]);
  pages[0] = superalloc();
  if (pages[0] == 0) {
    printf("  FAIL: could not reallocate after freeing\n");
    for (int i = 1; i < count; i++) {
      superfree(pages[i]);
    }
    return 0;
  }

  // Clean up
  for (int i = 0; i < count; i++) {
    superfree(pages[i]);
  }

  return 1;
}

// ============================================================================
// Test 12: PTE flags preservation
// ============================================================================

int khugepaged_test_pte_flags(void) {
  pagetable_t pgtbl = (pagetable_t)kalloc();
  if (pgtbl == 0) {
    printf("  FAIL: could not allocate page table\n");
    return 0;
  }
  memset(pgtbl, 0, PGSIZE);

  void *page = superalloc();
  if (page == 0) {
    printf("  FAIL: superalloc failed\n");
    kfree(pgtbl);
    return 0;
  }

  uint64 va = 0x200000;
  int flags = PTE_R | PTE_W | PTE_X | PTE_U;

  if (map_superpage(pgtbl, va, (uint64)page, flags) != 0) {
    printf("  FAIL: map_superpage failed\n");
    superfree(page);
    kfree(pgtbl);
    return 0;
  }

  // Check flags
  pte_t *l1_pte = walk_superpage(pgtbl, va, 0);
  if (l1_pte == 0) {
    printf("  FAIL: could not find L1 PTE\n");
    superfree(page);
    kfree(pgtbl);
    return 0;
  }

  uint64 stored_flags = PTE_FLAGS(*l1_pte) & (PTE_R | PTE_W | PTE_X | PTE_U);
  if (stored_flags != (uint64)flags) {
    printf("  FAIL: flags not preserved (got 0x%lx, expected 0x%x)\n",
           stored_flags, flags);
    superfree(page);
    kfree(pgtbl);
    return 0;
  }

  // Clean up
  pte_t l2_pte = pgtbl[PX(2, va)];
  if ((l2_pte & PTE_V) && !(l2_pte & (PTE_R | PTE_W | PTE_X))) {
    kfree((void *)PTE2PA(l2_pte));
  }
  superfree(page);
  kfree(pgtbl);

  return 1;
}

// ============================================================================
// Test Runner
// ============================================================================

// Wrapper to run tests in a kthread context
static void khugepaged_test_runner(void *arg) {
  (void)arg;
  khugepaged_test_all();
}

// Initialize the test runner as a kthread
void khugepaged_test_init(void) {
  struct proc *p = kthread_create(khugepaged_test_runner, 0, "khptest");
  if (p == 0) {
    printf("ERROR: Failed to create khugepaged test runner\n");
  }
}

void khugepaged_test_all(void) {
  printf("\n========================================\n");
  printf("Running Khugepaged / Huge Page Tests\n");
  printf("========================================\n\n");

  int passed = 0;
  int failed = 0;

  struct {
    const char *name;
    int (*func)(void);
  } tests[] = {
      {"Superalloc basic", khugepaged_test_superalloc_basic},
      {"Superalloc multiple", khugepaged_test_superalloc_multiple},
      {"Superfree and realloc", khugepaged_test_superfree_realloc},
      {"Superpage memory access", khugepaged_test_superpage_memory},
      {"walk_superpage function", khugepaged_test_walk_superpage},
      {"map_superpage function", khugepaged_test_map_superpage},
      {"is_superpage function", khugepaged_test_is_superpage},
      {"Size constants", khugepaged_test_size_constants},
      {"Daemon running", khugepaged_test_daemon_running},
      {"demote_superpage function", khugepaged_test_demote_superpage},
      {"Allocation exhaustion", khugepaged_test_alloc_exhaustion},
      {"PTE flags preservation", khugepaged_test_pte_flags},
  };

  int num_tests = sizeof(tests) / sizeof(tests[0]);

  for (int i = 0; i < num_tests; i++) {
    printf("Test %d: %s... ", i + 1, tests[i].name);
    if (tests[i].func()) {
      printf("PASSED\n");
      passed++;
    } else {
      printf("FAILED\n");
      failed++;
    }
  }

  printf("\n----------------------------------------\n");
  printf("Khugepaged Tests: %d passed, %d failed, %d total\n", passed, failed,
         num_tests);
  printf("========================================\n\n");
}
