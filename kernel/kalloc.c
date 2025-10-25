// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "kalloc.h"

#include "memlayout.h"
#include "printf.h"
#include "riscv.h"
#include "spinlock.h"
#include "string.h"
#include "types.h"

void freerange(void *pa_start, void *pa_end);

extern char end[];  // first address after kernel.
                    // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// Superpage allocator for 2MB pages
#define NSUPERPAGES 8  // Number of 2MB superpages to reserve

struct superrun {
  struct superrun *next;
};

struct {
  struct spinlock lock;
  struct superrun *freelist;
  char *superpage_start;  // Start of reserved superpage region
} supermem;

void kinit() {
  initlock(&kmem.lock, "kmem");
  initlock(&supermem.lock, "supermem");

  // Reserve 2MB-aligned region for superpages
  // Start from the first 2MB-aligned address after 'end'
  char *p = (char *)SUPERPGROUNDUP((uint64)end);
  supermem.superpage_start = p;
  supermem.freelist = 0;

  // Add NSUPERPAGES 2MB pages to superpage freelist
  for (int i = 0; i < NSUPERPAGES; i++) {
    struct superrun *r = (struct superrun *)p;
    r->next = supermem.freelist;
    supermem.freelist = r;
    p += SUPERPGSIZE;
  }

  // Free the rest to normal page allocator
  freerange(p, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end) {
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE) kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa) {
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP) {
    printf("kfree bad pa=%p end=%p PHYSTOP=0x%x\n", pa, end, PHYSTOP);
    panic("kfree");
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *kalloc(void) {
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r) kmem.freelist = r->next;
  release(&kmem.lock);

  if (r) memset((char *)r, 5, PGSIZE);  // fill with junk
  return (void *)r;
}

// Allocate one 2MB superpage of physical memory.
// Returns a 2MB-aligned pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *superalloc(void) {
  struct superrun *r;

  acquire(&supermem.lock);
  r = supermem.freelist;
  if (r) supermem.freelist = r->next;
  release(&supermem.lock);

  if (r) memset((char *)r, 0, SUPERPGSIZE);  // zero out the superpage
  return (void *)r;
}

// Free a 2MB superpage of physical memory pointed at by pa.
// pa must be 2MB-aligned.
void superfree(void *pa) {
  struct superrun *r;

  if (((uint64)pa % SUPERPGSIZE) != 0 || (char *)pa < supermem.superpage_start ||
      (uint64)pa >= (uint64)(supermem.superpage_start + NSUPERPAGES * SUPERPGSIZE)) {
    printf("superfree bad pa=%p\n", pa);
    panic("superfree");
  }

  // Fill with junk to catch dangling refs
  memset(pa, 1, SUPERPGSIZE);

  r = (struct superrun *)pa;

  acquire(&supermem.lock);
  r->next = supermem.freelist;
  supermem.freelist = r;
  release(&supermem.lock);
}
