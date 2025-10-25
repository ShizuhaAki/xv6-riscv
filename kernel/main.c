#include "bio.h"
#include "console.h"
#include "file.h"
#include "fs.h"
#include "kalloc.h"
#include "plic.h"
#include "printf.h"
#include "proc.h"
#include "slab.h"
#include "trap.h"
#include "virtio_disk.h"
#include "vm.h"

// Compile-time flag to enable slab allocator tests (default: OFF)
// To enable: add -DENABLE_SLAB_TESTS to CFLAGS in Makefile
#ifdef ENABLE_SLAB_TESTS
#include "test/slab_test_benchmark.h"
#include "test/slab_test_multi.h"
#include "test/slab_test_single.h"
#endif

volatile static int started = 0;
volatile static int prepared_device = 0;

// start() jumps here in supervisor mode on all CPUs.
void main() {
  if (cpuid() == 0) {
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();             // physical page allocator
    kvminit();           // create kernel page table
    kvminithart();       // turn on paging
    procinit();          // process table
    trapinit();          // trap vectors
    trapinithart();      // install kernel trap vector
    plicinit();          // set up interrupt controller
    plicinithart();      // ask PLIC for device interrupts
    binit();             // buffer cache
    iinit();             // inode table
    fileinit();          // file table
    virtio_disk_init();  // emulated hard disk
    userinit();          // first user process

#ifdef ENABLE_SLAB_TESTS
    slab_test_single();
#endif
    __sync_synchronize();
    started = 1;
#ifdef ENABLE_SLAB_TESTS
    __sync_fetch_and_add(&prepared_device, 1);
#endif
  } else {
    while (started == 0);
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();   // turn on paging
    trapinithart();  // install kernel trap vector
    plicinithart();  // ask PLIC for device interrupts
#ifdef ENABLE_SLAB_TESTS
    __sync_fetch_and_add(&prepared_device, 1);
#endif
  }

#ifdef ENABLE_SLAB_TESTS
  while (prepared_device < 3);
  slab_test_multi();
  if (cpuid() == 0) {
    slab_test_benchmark();
  }
#endif

  scheduler();
}
