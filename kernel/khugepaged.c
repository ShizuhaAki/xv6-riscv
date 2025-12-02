// khugepaged - Kernel daemon for transparent huge page management.
//
// This daemon periodically scans user processes and attempts to "collapse"
// 512 consecutive 4KB pages into a single 2MB superpage for better TLB
// efficiency.

#include "khugepaged.h"

#include "kalloc.h"
#include "memlayout.h"
#include "param.h"
#include "printf.h"
#include "proc.h"
#include "riscv.h"
#include "spinlock.h"
#include "string.h"
#include "trap.h"
#include "types.h"
#include "vm.h"

// Configuration
#define KHUGEPAGED_SCAN_INTERVAL 100  // Ticks between scans
#define KHUGEPAGED_DEBUG 0            // Set to 1 for verbose output

// External declarations
extern struct proc proc[];
extern struct spinlock tickslock;
extern uint ticks;

// Statistics
static uint64 pages_collapsed = 0;
static uint64 collapse_failures = 0;

// Forward declarations
static void khugepaged_scan_all_procs(void);
static void khugepaged_scan_pagetable(struct proc *p);
static int try_collapse_pages(struct proc *p, uint64 va);

// Main khugepaged daemon loop.
// Runs as a kernel thread, periodically scanning for collapse opportunities.
void khugepaged_main(void *arg) {
  (void)arg;  // Unused

  printf("khugepaged: daemon started\n");

  while (1) {
    // Scan all processes for collapse opportunities.
    khugepaged_scan_all_procs();

    // Sleep for a while before next scan.
    acquire(&tickslock);
    uint start_tick = ticks;
    while (ticks - start_tick < KHUGEPAGED_SCAN_INTERVAL) {
      sleep(&ticks, &tickslock);
    }
    release(&tickslock);
  }
}

// Scan all non-kthread processes for collapse opportunities.
static void khugepaged_scan_all_procs(void) {
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);

    // Skip unused, zombie, or kernel thread processes.
    if (p->state == UNUSED || p->state == ZOMBIE || p->is_kthread) {
      release(&p->lock);
      continue;
    }

    // Skip if no user page table.
    if (p->pagetable == 0) {
      release(&p->lock);
      continue;
    }

    // Scan this process's page table.
    khugepaged_scan_pagetable(p);

    release(&p->lock);
  }
}

// Scan a process's page table for 2MB regions that can be collapsed.
static void khugepaged_scan_pagetable(struct proc *p) {
  pagetable_t l2_table = p->pagetable;

  // Iterate through L2 (root) page table entries.
  // Each L2 entry covers 1GB (512 L1 entries * 2MB each).
  for (int l2_idx = 0; l2_idx < 512; l2_idx++) {
    pte_t l2_pte = l2_table[l2_idx];

    // Skip invalid entries.
    if (!(l2_pte & PTE_V)) {
      continue;
    }

    // If L2 is a 1GB superpage (very rare in xv6), skip.
    if (l2_pte & (PTE_R | PTE_W | PTE_X)) {
      continue;
    }

    // L2 points to an L1 table.
    pagetable_t l1_table = (pagetable_t)PTE2PA(l2_pte);

    // Iterate through L1 entries.
    // Each L1 entry covers 2MB (512 L0 entries * 4KB each).
    for (int l1_idx = 0; l1_idx < 512; l1_idx++) {
      pte_t l1_pte = l1_table[l1_idx];

      // Skip invalid entries.
      if (!(l1_pte & PTE_V)) {
        continue;
      }

      // If already a 2MB superpage, skip.
      if (l1_pte & (PTE_R | PTE_W | PTE_X)) {
        continue;
      }

      // L1 points to an L0 table - this is a candidate for collapsing.
      uint64 va = ((uint64)l2_idx << 30) | ((uint64)l1_idx << 21);

      // Only collapse pages within user memory range.
      if (va >= p->sz || va >= USYSCALL) {
        continue;
      }

      // Check if the entire 2MB range is within process memory.
      if (va + SUPERPGSIZE > p->sz && va + SUPERPGSIZE <= USYSCALL) {
        continue;
      }

      // Try to collapse this 2MB region.
      if (try_collapse_pages(p, va) == 0) {
        pages_collapsed++;
#if KHUGEPAGED_DEBUG
        printf("khugepaged: collapsed 2MB at va 0x%lx for pid %d\n", va, p->pid);
#endif
      } else {
        collapse_failures++;
      }
    }
  }
}

// Try to collapse 512 4KB pages at va into a single 2MB superpage.
// Returns 0 on success, -1 on failure.
// Caller must hold p->lock.
static int try_collapse_pages(struct proc *p, uint64 va) {
  pagetable_t pagetable = p->pagetable;
  pte_t *l1_pte;
  pagetable_t l0_table;
  uint64 perm = 0;
  int all_present = 1;
  int all_same_perm = 1;

  // Verify va is 2MB aligned.
  if (va % SUPERPGSIZE != 0) {
    return -1;
  }

  // Get the L1 PTE.
  l1_pte = walk_superpage(pagetable, va, 0);
  if (l1_pte == 0 || !(*l1_pte & PTE_V)) {
    return -1;
  }

  // Must be pointing to L0 table, not already a superpage.
  if (*l1_pte & (PTE_R | PTE_W | PTE_X)) {
    return -1;
  }

  l0_table = (pagetable_t)PTE2PA(*l1_pte);

  // Check all 512 L0 entries.
  for (int i = 0; i < 512; i++) {
    pte_t l0_pte = l0_table[i];

    // All pages must be present.
    if (!(l0_pte & PTE_V)) {
      all_present = 0;
      break;
    }

    // All pages must be user-accessible.
    if (!(l0_pte & PTE_U)) {
      all_present = 0;
      break;
    }

    // Track permissions - they should all be the same for a superpage.
    uint64 this_perm = PTE_FLAGS(l0_pte) & (PTE_R | PTE_W | PTE_X | PTE_U);
    if (i == 0) {
      perm = this_perm;
    } else if (this_perm != perm) {
      all_same_perm = 0;
      break;
    }
  }

  // Can't collapse if not all pages are present or permissions differ.
  if (!all_present || !all_same_perm) {
    return -1;
  }

  // Allocate a new 2MB superpage.
  char *new_page = superalloc();
  if (new_page == 0) {
    return -1;  // No superpage available.
  }

  // Copy data from 512 4KB pages to the new superpage.
  for (int i = 0; i < 512; i++) {
    pte_t l0_pte = l0_table[i];
    char *old_page = (char *)PTE2PA(l0_pte);
    memmove(new_page + i * PGSIZE, old_page, PGSIZE);
  }

  // Update the L1 PTE to point to the superpage.
  // Clear the old L1 entry first.
  pte_t old_l1 = *l1_pte;
  *l1_pte = PA2PTE((uint64)new_page) | perm | PTE_V;

  // Flush TLB for this address range.
  sfence_vma();

  // Free the old 512 4KB pages and the L0 table.
  for (int i = 0; i < 512; i++) {
    pte_t l0_pte = l0_table[i];
    kfree((void *)PTE2PA(l0_pte));
  }
  kfree((void *)PTE2PA(old_l1));

  return 0;
}

// Print khugepaged statistics.
void khugepaged_stats(void) {
  printf("khugepaged stats:\n");
  printf("  pages collapsed: %ld\n", pages_collapsed);
  printf("  collapse failures: %ld\n", collapse_failures);
}

// Start the khugepaged daemon.
void khugepaged_init(void) {
  struct proc *p = kthread_create(khugepaged_main, 0, "khugepaged");
  if (p == 0) {
    printf("khugepaged: failed to create daemon\n");
  }
}
