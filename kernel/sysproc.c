#include "proc.h"
#include "spinlock.h"
#include "syscall.h"
#include "trap.h"
#include "types.h"
#include "vm.h"
#include "fcntl.h"
#include "file.h"
#include "fs.h"
#include "sleeplock.h"
#include "memlayout.h"
#include "log.h"

uint64 sys_exit(void) {
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64 sys_getpid(void) { return myproc()->pid; }

uint64 sys_fork(void) { return kfork(); }

uint64 sys_wait(void) {
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64 sys_sbrk(void) {
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if (t == SBRK_EAGER || n < 0) {
    if (growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr) return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64 sys_pause(void) {
  int n;
  uint ticks0;

  argint(0, &n);
  if (n < 0) n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64 sys_kill(void) {
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void) {
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// Find an unused VMA slot
static struct vma* vma_alloc(struct proc *p) {
  for (int i = 0; i < NVMA; i++) {
    if (!p->vmas[i].used) {
      return &p->vmas[i];
    }
  }
  return 0;
}

// Find an unused region in process address space for mmap
// Returns address or 0 on failure
static uint64 vma_find_addr(struct proc *p, uint64 len) {
  uint64 addr = PGROUNDUP(p->sz);

  // Check each VMA to avoid overlaps
  for (int i = 0; i < NVMA; i++) {
    if (p->vmas[i].used) {
      uint64 vma_end = p->vmas[i].addr + p->vmas[i].len;
      if (addr < vma_end) {
        addr = PGROUNDUP(vma_end);
      }
    }
  }

  // Make sure we don't overlap with USYSCALL or TRAPFRAME
  if (addr + len > USYSCALL) {
    return 0;  // No space
  }

  return addr;
}

// Write back allocated pages in a VMA region to file
// Only writes pages that have been allocated (lazy allocation)
static void vma_writeback(struct vma *v, uint64 addr, uint64 len) {
  if (!(v->flags & MAP_SHARED)) {
    return;  // Only write back MAP_SHARED
  }

  struct proc *p = myproc();
  struct inode *ip = v->file->ip;

  // Lock inode to read size
  ilock(ip);
  uint file_size = ip->size;
  iunlock(ip);

  for (uint64 va = addr; va < addr + len; va += PGSIZE) {
    // Check if this page is allocated
    if (ismapped(p->pagetable, va)) {
      // Calculate offset in file
      uint64 offset_in_vma = va - v->addr;
      uint64 file_offset = v->offset + offset_in_vma;

      // Don't write beyond the current file size
      if (file_offset >= file_size) {
        continue;
      }

      // Calculate how many bytes to write
      uint64 bytes_to_write = PGSIZE;
      if (file_offset + bytes_to_write > file_size) {
        bytes_to_write = file_size - file_offset;
      }

      if (bytes_to_write > 0) {
        begin_op();
        ilock(ip);
        writei(ip, 1, va, file_offset, bytes_to_write);
        iunlock(ip);
        end_op();
      }
    }
  }
}

uint64 sys_mmap(void) {
  uint64 addr;
  int len, prot, flags, fd;
  uint64 offset;
  struct file *f;
  struct proc *p = myproc();
  struct vma *v;

  // Get arguments
  argaddr(0, &addr);
  argint(1, &len);
  argint(2, &prot);
  argint(3, &flags);
  argint(4, &fd);
  argaddr(5, &offset);

  // Validate arguments
  if (addr != 0) {
    return -1;  // We don't support non-zero addr
  }
  if (len <= 0) {
    return -1;
  }
  if (fd < 0 || fd >= NOFILE || (f = p->ofile[fd]) == 0) {
    return -1;  // Invalid file descriptor
  }
  if (!f->readable && (prot & PROT_READ)) {
    return -1;  // Can't map unreadable file as readable
  }
  if (!f->writable && (prot & PROT_WRITE) && (flags & MAP_SHARED)) {
    return -1;  // Can't map unwritable file as MAP_SHARED writable
  }

  // Find unused VMA slot
  if ((v = vma_alloc(p)) == 0) {
    return -1;  // No free VMA slots
  }

  // Find address space for mapping
  if ((addr = vma_find_addr(p, len)) == 0) {
    return -1;  // No space in address space
  }

  // Set up VMA
  v->used = 1;
  v->addr = addr;
  v->len = len;
  v->prot = prot;
  v->flags = flags;
  v->file = filedup(f);  // Increment file reference count
  v->offset = offset;

  return addr;
}

uint64 sys_munmap(void) {
  uint64 addr;
  int len;
  struct proc *p = myproc();

  argaddr(0, &addr);
  argint(1, &len);

  if (len <= 0 || addr % PGSIZE != 0) {
    return -1;
  }

  // Find and unmap the VMA(s)
  for (int i = 0; i < NVMA; i++) {
    struct vma *v = &p->vmas[i];
    if (!v->used) continue;

    uint64 vma_end = v->addr + v->len;
    uint64 unmap_end = addr + len;

    // Check if this VMA overlaps with the unmap region
    if (addr >= vma_end || unmap_end <= v->addr) {
      continue;  // No overlap
    }

    // Handle different unmap scenarios
    if (addr <= v->addr && unmap_end >= vma_end) {
      // Unmapping entire VMA
      // Write back dirty pages if MAP_SHARED
      vma_writeback(v, v->addr, v->len);

      // Unmap all pages
      uvmunmap(p->pagetable, v->addr, v->len / PGSIZE, 1);

      // Release file reference
      fileclose(v->file);

      // Mark VMA as free
      v->used = 0;
    } else if (addr == v->addr) {
      // Unmapping from start
      vma_writeback(v, v->addr, len);

      uvmunmap(p->pagetable, v->addr, len / PGSIZE, 1);

      // Adjust VMA
      v->addr += len;
      v->len -= len;
      v->offset += len;
    } else if (unmap_end == vma_end) {
      // Unmapping from end
      uint64 write_addr = v->addr + v->len - len;
      vma_writeback(v, write_addr, len);

      uvmunmap(p->pagetable, write_addr, len / PGSIZE, 1);

      // Adjust VMA
      v->len -= len;
    }
    // Note: We don't handle unmapping from middle (punching holes)
    // as the lab says we can assume this won't happen
  }

  return 0;
}
