#include "bio.h"
#include "console.h"
#include "file.h"
#include "fs.h"
#include "kalloc.h"
#include "plic.h"
#include "printf.h"
#include "proc.h"
#include "slab.h"
#include "spinlock.h"
#include "test/slab_test_multi.h"
#include "test/slab_test_single.h"
#include "trap.h"
#include "virtio_disk.h"
#include "vm.h"

volatile static int started = 0;
volatile static int prepared_device = 0;
struct spinlock prepared_device_lock;

// start() jumps here in supervisor mode on all CPUs.
void main() {
  if (cpuid() == 0) {
    initlock(&prepared_device_lock, "prepared_device_lock");
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

    slab_test_single();
    __sync_synchronize();
    started = 1;
    acquire(&prepared_device_lock);
    prepared_device += 1;
    release(&prepared_device_lock);
  } else {
    while (started == 0);
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();   // turn on paging
    trapinithart();  // install kernel trap vector
    plicinithart();  // ask PLIC for device interrupts
    acquire(&prepared_device_lock);
    prepared_device += 1;
    release(&prepared_device_lock);
  }

  while (prepared_device < 3);
  slab_test_multi();
  scheduler();
}
