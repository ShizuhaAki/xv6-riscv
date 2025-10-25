#include "vm.h"

#include "kalloc.h"
#include "memlayout.h"
#include "printf.h"
#include "proc.h"
#include "riscv.h"
#include "string.h"
#include "types.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[];  // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t kvmmake(void) {
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t)kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext,
         PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);

  return kpgtbl;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm) {
  if (mappages(kpgtbl, va, sz, pa, perm) != 0) panic("kvmmap");
}

// Initialize the kernel_pagetable, shared by all CPUs.
void kvminit(void) { kernel_pagetable = kvmmake(); }

// Switch the current CPU's h/w page table register to
// the kernel's page table, and enable paging.
void kvminithart() {
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc) {
  if (va >= MAXVA) panic("walk");

  for (int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if (*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if (!alloc || (pagetable = (pde_t *)kalloc()) == 0) return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Return the level-1 PTE for a superpage (2MB page).
// If alloc!=0, create the level-2 page table if needed.
// This is used to create superpages, which use level-1 PTEs.
pte_t *walk_superpage(pagetable_t pagetable, uint64 va, int alloc) {
  if (va >= MAXVA) panic("walk_superpage");

  // Only traverse to level 2, then return level-1 PTE
  pte_t *pte = &pagetable[PX(2, va)];
  if (*pte & PTE_V) {
    pagetable = (pagetable_t)PTE2PA(*pte);
  } else {
    if (!alloc || (pagetable = (pde_t *)kalloc()) == 0) return 0;
    memset(pagetable, 0, PGSIZE);
    *pte = PA2PTE(pagetable) | PTE_V;
  }
  return &pagetable[PX(1, va)];
}

// Check if a PTE represents a superpage (level-1 PTE with R/W/X bits set)
int is_superpage(pte_t pte) {
  return (pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X));
}

// Map a single 2MB superpage.
// va and pa must be 2MB-aligned.
// Returns 0 on success, -1 on failure.
int map_superpage(pagetable_t pagetable, uint64 va, uint64 pa, int perm) {
  pte_t *pte;

  if ((va % SUPERPGSIZE) != 0) panic("map_superpage: va not aligned");
  if ((pa % SUPERPGSIZE) != 0) panic("map_superpage: pa not aligned");

  if ((pte = walk_superpage(pagetable, va, 1)) == 0) return -1;
  if (*pte & PTE_V) panic("map_superpage: remap");

  // Set the level-1 PTE to point to the 2MB physical page
  *pte = PA2PTE(pa) | perm | PTE_V;
  return 0;
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64 walkaddr(pagetable_t pagetable, uint64 va) {
  pte_t *pte;
  uint64 pa;

  if (va >= MAXVA) return 0;

  pte = walk(pagetable, va, 0);
  if (pte == 0) return 0;
  if ((*pte & PTE_V) == 0) return 0;
  if ((*pte & PTE_U) == 0) return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa.
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa,
             int perm) {
  uint64 a, last;
  pte_t *pte;

  if ((va % PGSIZE) != 0) panic("mappages: va not aligned");

  if ((size % PGSIZE) != 0) panic("mappages: size not aligned");

  if (size == 0) panic("mappages: size");

  a = va;
  last = va + size - PGSIZE;
  for (;;) {
    if ((pte = walk(pagetable, a, 1)) == 0) return -1;
    if (*pte & PTE_V) panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if (a == last) break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t uvmcreate() {
  pagetable_t pagetable;
  pagetable = (pagetable_t)kalloc();
  if (pagetable == 0) return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Demote a superpage to regular 4KB pages.
// This is needed when partially freeing a superpage.
// va must be 2MB-aligned and point to a valid superpage.
// Returns 0 on success, -1 on failure.
int demote_superpage(pagetable_t pagetable, uint64 va) {
  pte_t *pte_l1;
  uint64 pa;
  uint flags;

  if ((va % SUPERPGSIZE) != 0) panic("demote_superpage: va not aligned");

  // Get the level-1 PTE for this superpage
  pte_l1 = walk_superpage(pagetable, va, 0);
  if (pte_l1 == 0 || (*pte_l1 & PTE_V) == 0) {
    panic("demote_superpage: no superpage");
  }
  if (!is_superpage(*pte_l1)) {
    panic("demote_superpage: not a superpage");
  }

  // Get the physical address and flags
  pa = PTE2PA(*pte_l1);
  flags = PTE_FLAGS(*pte_l1);

  // Clear the level-1 PTE
  *pte_l1 = 0;

  // Map each 4KB page individually
  for (uint64 i = 0; i < SUPERPGSIZE; i += PGSIZE) {
    if (mappages(pagetable, va + i, PGSIZE, pa + i, flags) != 0) {
      // Rollback: restore superpage mapping
      *pte_l1 = PA2PTE(pa) | flags;
      return -1;
    }
  }

  // The physical memory is still there, mapped as 512 individual 4KB pages
  // The caller is responsible for freeing pages as needed
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. It's OK if the mappings don't exist.
// Optionally free the physical memory.
// Handles both regular pages and superpages.
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free) {
  uint64 a;
  pte_t *pte;

  if ((va % PGSIZE) != 0) panic("uvmunmap: not aligned");

  for (a = va; a < va + npages * PGSIZE;) {
    // Check if this is a superpage
    uint64 superpage_addr = SUPERPGROUNDDOWN(a);
    pte_t *pte_l1 = walk_superpage(pagetable, superpage_addr, 0);

    if (pte_l1 != 0 && (*pte_l1 & PTE_V) && is_superpage(*pte_l1)) {
      // This is a superpage
      uint64 superpage_end = superpage_addr + SUPERPGSIZE;
      uint64 unmap_end = va + npages * PGSIZE;

      // Check if we're unmapping the entire superpage
      if (a == superpage_addr && unmap_end >= superpage_end) {
        // Unmapping entire superpage
        if (do_free) {
          uint64 pa = PTE2PA(*pte_l1);
          superfree((void *)pa);
        }
        *pte_l1 = 0;
        a = superpage_end;
        continue;
      } else {
        // Partially unmapping superpage - need to demote
        if (demote_superpage(pagetable, superpage_addr) != 0) {
          panic("uvmunmap: demote failed");
        }
        // Now fall through to regular page handling
      }
    }

    // Handle regular page
    if ((pte = walk(pagetable, a, 0)) == 0)  // leaf page table entry allocated?
    {
      a += PGSIZE;
      continue;
    }
    if ((*pte & PTE_V) == 0)  // has physical page been allocated?
    {
      a += PGSIZE;
      continue;
    }
    if (do_free) {
      uint64 pa = PTE2PA(*pte);
      kfree((void *)pa);
    }
    *pte = 0;
    a += PGSIZE;
  }
}

// Allocate PTEs and physical memory to grow a process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
// Uses superpages (2MB pages) when possible for better performance.
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm) {
  char *mem;
  uint64 a;

  if (newsz < oldsz) return oldsz;

  oldsz = PGROUNDUP(oldsz);

  // Try to use superpages for 2MB-aligned regions
  for (a = oldsz; a < newsz;) {
    // Check if we can use a superpage at this address
    uint64 superpage_start = SUPERPGROUNDUP(a);
    uint64 superpage_end = superpage_start + SUPERPGSIZE;

    // Can use superpage if:
    // 1. We have at least 2MB left to allocate
    // 2. The superpage fits entirely within the allocation range
    if (superpage_start < newsz && superpage_end <= newsz) {
      // Fill any gap before the superpage with regular pages
      while (a < superpage_start) {
        mem = kalloc();
        if (mem == 0) {
          uvmdealloc(pagetable, a, oldsz);
          return 0;
        }
        memset(mem, 0, PGSIZE);
        if (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R | PTE_U | xperm) !=
            0) {
          kfree(mem);
          uvmdealloc(pagetable, a, oldsz);
          return 0;
        }
        a += PGSIZE;
      }

      // Try to allocate a superpage
      mem = superalloc();
      if (mem != 0) {
        // Successfully allocated superpage
        if (map_superpage(pagetable, a, (uint64)mem, PTE_R | PTE_U | xperm) !=
            0) {
          superfree(mem);
          uvmdealloc(pagetable, a, oldsz);
          return 0;
        }
        a += SUPERPGSIZE;
        continue;
      }
      // If superpage allocation failed, fall back to regular pages
    }

    // Use regular page
    mem = kalloc();
    if (mem == 0) {
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R | PTE_U | xperm) !=
        0) {
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    a += PGSIZE;
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
  if (newsz >= oldsz) return oldsz;

  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void freewalk(pagetable_t pagetable) {
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if (pte & PTE_V) {
      panic("freewalk: leaf");
    }
  }
  kfree((void *)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void uvmfree(pagetable_t pagetable, uint64 sz) {
  if (sz > 0) uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
// Handles both regular pages and superpages.
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for (i = 0; i < sz;) {
    // Check if this address is part of a superpage
    uint64 superpage_addr = SUPERPGROUNDDOWN(i);
    pte_t *pte_l1 = walk_superpage(old, superpage_addr, 0);

    if (pte_l1 != 0 && (*pte_l1 & PTE_V) && is_superpage(*pte_l1)) {
      // This is a superpage - copy the entire 2MB page
      pa = PTE2PA(*pte_l1);
      flags = PTE_FLAGS(*pte_l1);

      // Allocate a new superpage
      mem = superalloc();
      if (mem == 0) goto err;

      // Copy the entire 2MB
      memmove(mem, (char *)pa, SUPERPGSIZE);

      // Map the new superpage
      if (map_superpage(new, superpage_addr, (uint64)mem, flags) != 0) {
        superfree(mem);
        goto err;
      }

      // Skip to the next address after this superpage
      i = superpage_addr + SUPERPGSIZE;
      continue;
    }

    // Handle regular page
    if ((pte = walk(old, i, 0)) == 0) {
      i += PGSIZE;
      continue;  // page table entry hasn't been allocated
    }
    if ((*pte & PTE_V) == 0) {
      i += PGSIZE;
      continue;  // physical page hasn't been allocated
    }
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if ((mem = kalloc()) == 0) goto err;
    memmove(mem, (char *)pa, PGSIZE);
    if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0) {
      kfree(mem);
      goto err;
    }
    i += PGSIZE;
  }
  return 0;

err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void uvmclear(pagetable_t pagetable, uint64 va) {
  pte_t *pte;

  pte = walk(pagetable, va, 0);
  if (pte == 0) panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len) {
  uint64 n, va0, pa0;
  pte_t *pte;

  while (len > 0) {
    va0 = PGROUNDDOWN(dstva);
    if (va0 >= MAXVA) return -1;

    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) {
      if ((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }

    pte = walk(pagetable, va0, 0);
    // forbid copyout over read-only user text pages.
    if ((*pte & PTE_W) == 0) return -1;

    n = PGSIZE - (dstva - va0);
    if (n > len) n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len) {
  uint64 n, va0, pa0;

  while (len > 0) {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) {
      if ((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }
    n = PGSIZE - (srcva - va0);
    if (n > len) n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max) {
  uint64 n, va0, pa0;
  int got_null = 0;

  while (got_null == 0 && max > 0) {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) return -1;
    n = PGSIZE - (srcva - va0);
    if (n > max) n = max;

    char *p = (char *)(pa0 + (srcva - va0));
    while (n > 0) {
      if (*p == '\0') {
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if (got_null) {
    return 0;
  } else {
    return -1;
  }
}

// allocate and map user memory if process is referencing a page
// that was lazily allocated in sys_sbrk().
// returns 0 if va is invalid or already mapped, or if
// out of physical memory, and physical address if successful.
uint64 vmfault(pagetable_t pagetable, uint64 va, int read) {
  uint64 mem;
  struct proc *p = myproc();

  if (va >= p->sz) return 0;
  va = PGROUNDDOWN(va);
  if (ismapped(pagetable, va)) {
    return 0;
  }
  mem = (uint64)kalloc();
  if (mem == 0) return 0;
  memset((void *)mem, 0, PGSIZE);
  if (mappages(p->pagetable, va, PGSIZE, mem, PTE_W | PTE_U | PTE_R) != 0) {
    kfree((void *)mem);
    return 0;
  }
  return mem;
}

int ismapped(pagetable_t pagetable, uint64 va) {
  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0) {
    return 0;
  }
  if (*pte & PTE_V) {
    return 1;
  }
  return 0;
}
