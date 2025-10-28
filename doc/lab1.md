# Lab 1

| Name            | Student ID  | Email        | Date      |
| --------------- | ----------- | ------------ | --------- |
| Zecyel (朱程炀) | 23300240014 | i@zecyel.xyz | 2025.10.24 |

## Task 1: Speed Up System Calls

### 思路

给进程创建一个 usyscall 页，挂载在 trapframe 旁边。在创建/删除进程，创建/删除页表的时候做对应处理。然后实现纯用户态的 `ugetpid` 函数。

### 计算用户空间地址

```c
#define TRAPFRAME (TRAMPOLINE - PGSIZE)
#define USYSCALL (TRAPFRAME - PGSIZE)

#ifndef __ASSEMBLER__
// Shared data structure for fast syscalls
struct usyscall {
  int pid;  // Process ID
};
#endif
```

### 创建 usyscall 页

```c
// Allocate a usyscall page.
if ((p->usyscall = (struct usyscall *)kalloc()) == 0) {
  freeproc(p);
  release(&p->lock);
  return 0;
}
// Initialize usyscall with the process PID
p->usyscall->pid = p->pid;
```

### 给虚拟地址映射到物理地址

```c
// map the usyscall page just below the trapframe page.
// read-only for userspace (PTE_R | PTE_U).
if (mappages(pagetable, USYSCALL, PGSIZE, (uint64)(p->usyscall),
             PTE_R | PTE_U) < 0) {
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, 0);
  return 0;
}
```

## Task2: Use Superpages

由于当前的页表设置支持 4K 页和 2M 页，但是内核只写了 4K 页，需要添加 2M 的超页支持。

需要修改的内容有：

1. 像 kmem 一样实现 supermem，以此类推实现 superalloc 等函数。
2. 支持 supermem 的对齐。
3. 实现 is_superpage 函数和 walk_superpage，这样就可以在访问超页的时候获取到它的基地址。
4. 当释放超页的一部分的时候，需要把它重新拆分成小页（为什么），实现 demote_superpage。
5. 为 uvmunmap、uvmalloc、uvmcopy 等函数提供超页支持。

## Task3: Mmap

目的是把一些内容直接映射到用户空间中，这样可以有很多优点，比如高效的文件读写，高效的进程间通信等等。

实现过程：

1. 实现 VMA 的相关操作，比如 vma_alloc，vma_find_addr 等。
2. 创建线程的时候先初始化 VMA，当 fork 的时候把 VMA 拷贝给儿子。
3. 在退出线程的时候需要把所有的 VMA unmap 了。
4. 如果遇到 pgfault 了，需要遍历 VMA 判断它是不是 mmap 的区域，如果是的话，使用 mmap 专用的加载。
