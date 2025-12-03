# Lab 3: Kernel Threads & Transparent Huge Pages

| Name            | Student ID  | Email        | Date      |
| --------------- | ----------- | ------------ | --------- |
| Zecyel (朱程炀) | 23300240014 | i@zecyel.xyz | 2026.01.12 |

## Overview

Following the style of `doc/lab1.md` and `doc/lab2.md`, this lab documents the design and implementation of two
tightly related features:

1. **Kernel threads (`kthread`)** – lightweight tasks that only run in kernel mode and execute background work
without a user address space.
2. **`khugepaged` daemon** – a dedicated kernel thread that enables transparent huge pages (THP) by collapsing
contiguous 4 KB pages into 2 MB mappings.

Together they bring xv6 closer to the Linux model described in the docs under `doc/`.

## Kernel Threads

### Data Structure Changes

- Added `is_kthread`, `kthread_func`, and `kthread_arg` to `struct proc` so the scheduler can distinguish kernel
threads from user processes and know which function to invoke.
- `allocproc()` now initializes the trapframe/context for kernel threads to return into `kthread_entry`, and it skips
allocating a user pagetable (`p->sz = 0`).
- A helper `kthread_create(void (*func)(void *), void *arg, char *name)` wraps allocation, initialization, and
RUNNABLE enqueueing.

```c
p->is_kthread = 1;
p->kthread_func = func;
p->context.ra = (uint64)kthread_entry;
p->context.sp = p->kstack + PGSIZE;
```

### Lifecycle Integration

- `kthread_entry()` releases `p->lock`, invokes `kthread_func`, and calls `exit(0)` when the function returns so
every kernel thread tears down cleanly.
- Guards in `fork()` and `exec()` (`panic("kthread fork")`, `panic("kthread exec")`) ensure kernel threads never
clone or run user programs.
- `exit()` skips user VM teardown when `is_kthread` is set, but it still wakes the parent and transitions to
`ZOMBIE`.
- `usertrap()` panics if it ever sees a kernel thread (`panic("kthread in usertrap")`), catching impossible
transitions early.

These changes keep kernel-only tasks isolated from user-facing lifecycle code paths.

## Buddy Allocator & Huge Page VM Support

### Physical Allocator

- Replaced the simple freelist allocator with a buddy system that tracks page blocks by order. This lets us allocate
and free contiguous power-of-two blocks up to 2 MB (order 9).
- Introduced `kalloc_order(int order)` / `kfree_order(void *pa, int order)` and the convenience wrappers
`kalloc_huge()` / `kfree_huge()` for 2 MB chunks.

### VM Changes

- `walk()` inspects L2 PTEs: if a PTE at that level has R/W/X set, it returns the L2 entry itself so callers can
observe an existing huge mapping.
- `mappages()` accepts a new flag to request huge mappings when the virtual address and length are 2 MB-aligned. It
installs the leaf entry directly in the L2 table.
- `uvmcopy`, `uvmunmap`, `uvmalloc`, and `uvmclear` handle both 4 KB and 2 MB leaves, demoting huge pages if only
part of the region is unmapped.
- Added `sfence_vma_addr(uint64 va)` to flush a single mapping; `sfence_vma()` remains available for global
invalidations.

These changes make it possible to install, clone, and remove 2 MB mappings without corrupting the walk logic used
elsewhere in the kernel.

## `khugepaged` Daemon

### Creation

- `main()` spawns the daemon after `userinit()` via `kthread_create(khugepaged_main, 0, "khugepaged")`, so the
scheduler always has a background scanner ready.

### Main Loop

```c
void khugepaged_main(void *arg) {
  for (;;) {
    scan_all_procs_for_collapse();
    acquire(&tickslock);
    sleep(&ticks, &tickslock);  // pace the scanner
    release(&tickslock);
  }
}
```

The daemon simply scans, sleeps on the global tick channel, and repeats.

### Scanning and Collapse

1. **Process walk**: Iterate over `proc[]`, skip zombies and all kernel threads, and hold each process lock while
peeking at its pagetable.
2. **L2 sweep**: For each 2 MB-aligned region below `p->sz`, look for an L2 PTE that points to an L1 table (i.e.,
PTE_V set but no R/W/X bits).
3. **Validation**: Ensure the corresponding 512 L1 entries are mapped and share compatible flags (R/W/U). If a hole
is found, skip the region.
4. **Collapse**:
   - Allocate a new 2 MB block with `kalloc_huge()`.
   - Copy every 4 KB page into the new block (`memmove(new_base + i*PGSIZE, old_page, PGSIZE)`).
   - Install the huge PTE (`PA2PTE(new_base) | (PTE_R|PTE_W|PTE_X|PTE_U|PTE_V)`).
   - `sfence_vma_addr(va)` to invalidate stale TLB entries.
   - Free all 512 old 4 KB pages plus the intermediate L1 table.
   - Emit a trace line (`printf("khugepaged: collapsed 0x%p\n", va);`) for observability.

This mirrors the Linux workflow outlined in the khugepaged guide while remaining simple enough for xv6.

## Testing

1. **`kthreadtest`** – Launches a small kernel thread that increments a counter and deliberately calls `sleep()`/
`exit()` to cover lifecycle code paths. Confirms kernel threads never reenter usertrap and cannot fork/exec.
2. **`khugepagedtest`** – User program that allocates a few megabytes, touches every 4 KB page, then sleeps to let
khugepaged run. Uses a `/proc/pagetable` helper to assert that the hot region flips from 512 small pages to a single
huge mapping, and validates data persists.
3. Regressions: ran `usertests`, `sigtest`, and the new THP tests to ensure previous labs (`doc/lab1.md`, `doc/
lab2.md`) still pass.

All tests passed on QEMU (both `make qemu` and `make qemu-gdb`), so the integration looks stable.

## Limitations & Future Work

- Collapse is optimistic: it does not pause the user process while copying, so concurrent writes can race. A full
solution would either pause the process or rely on copy-on-write faults.
- `khugepaged` currently scans linearly and collapses at most one region per pass. Adaptive pacing or per-process
queues (like Linux’s `khugepaged_scan`) would reduce CPU usage.
- No promotion of kernel heap or text segments yet; only user space benefits.
- Demotion only happens when `uvmunmap()` hits a partial huge page—there’s no proactive fragmentation handling.
- Buddy allocator is 2 MB-aware but not NUMA-aware; multiple CPUs contending for the same freelists could hurt
scalability.