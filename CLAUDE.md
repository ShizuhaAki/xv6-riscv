# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is xv6-riscv, a re-implementation of Unix Version 6 for modern RISC-V multiprocessors using ANSI C. It's a teaching operating system used for MIT's 6.1810 course. The codebase has been extended with a custom slab memory allocator implementation.

## Build and Development Commands

### Build and Run
```bash
make qemu          # Build kernel and run in QEMU emulator
make qemu CPUS=N   # Run with N CPUs (default is 3)
make clean         # Clean build artifacts
```

### Debugging
```bash
make qemu-gdb      # Run QEMU with GDB server enabled
make print-gdbport # Print the GDB port number
gdb                # In another terminal, connect with GDB
```

### Build Requirements
- RISC-V "newlib" toolchain (riscv64-unknown-elf-gcc or riscv64-linux-gnu-gcc)
- QEMU compiled for riscv64-softmmu (minimum version 7.2)

### Code Formatting
```bash
clang-format -i <file>  # Format a single file
```

The project uses Google style with custom settings in `.clang-format`.

## Architecture Overview

### Kernel Organization

**Core Memory Management:**
- `kernel/kalloc.c` - Physical page allocator using a free list of 4KB pages
- `kernel/slab.c/h` - Slab allocator for efficient small object allocation
  - Implements Linux-style slab allocator with partial/full/empty slab lists
  - Provides object-level caching with constructor/destructor support
  - Thread-safe with per-cache spinlocks
  - API: `kmem_cache_create()`, `kmem_cache_alloc()`, `kmem_cache_free()`, `kmem_cache_destroy()`
- `kernel/vm.c/h` - Virtual memory management and page tables

**Process Management:**
- `kernel/proc.c/h` - Process table, scheduler, context switching
- `kernel/swtch.S` - Low-level context switch code
- `kernel/trap.c/h` - Trap handling (interrupts, exceptions, syscalls)
- Process states: UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE

**File System (on-disk layout: boot | super | log | inodes | bitmap | data):**
- `kernel/fs.c/h` - File system implementation with indirect blocks
- `kernel/bio.c/h` - Block buffer cache
- `kernel/log.c/h` - Write-ahead logging for crash recovery
- `kernel/file.c/h` - File descriptor layer
- `kernel/pipe.c/h` - Pipe implementation

**System Calls:**
- `kernel/syscall.c/h` - Syscall dispatcher and argument helpers
- `kernel/sysproc.c` - Process-related syscalls (fork, exit, wait, etc.)
- `kernel/sysfile.c` - File system syscalls (open, read, write, etc.)
- `user/usys.pl` - Perl script that generates syscall stubs in `user/usys.S`

**Devices and I/O:**
- `kernel/console.c/h` - Console device driver
- `kernel/uart.c/h` - UART device for serial I/O
- `kernel/virtio_disk.c/h` - VirtIO disk driver for QEMU
- `kernel/plic.c/h` - Platform-Level Interrupt Controller

**Synchronization:**
- `kernel/spinlock.c/h` - Spinlocks for mutual exclusion
- `kernel/sleeplock.c/h` - Sleep locks for long-held locks

**Boot and Initialization:**
- `kernel/entry.S` - Entry point, sets up stack and jumps to start
- `kernel/start.c` - Machine mode initialization, then supervisor mode
- `kernel/main.c` - Main kernel initialization sequence:
  1. Console, printf, physical allocator (kinit)
  2. Virtual memory (kvminit)
  3. Process table, traps, interrupts
  4. Buffer cache, inode table, file table
  5. Disk driver initialization
  6. First user process (userinit)
  7. Slab allocator tests (single-threaded, multi-threaded, benchmarks)
  8. Scheduler

### User Space Organization

**User Programs** (`user/` directory):
- Standard Unix utilities: cat, echo, grep, ls, mkdir, rm, wc, sh
- Test programs: usertests, forktest, grind, stressfs, logstress
- `user/ulib.c` - User-space library functions
- `user/umalloc.c` - User-space malloc implementation
- `user/user.h` - User-space header with syscall declarations

### Testing Infrastructure

**Slab Allocator Tests** (in `kernel/test/`):
- `slab_test_single.c/h` - Single-threaded tests (10 tests including allocation, alignment, edge cases)
- `slab_test_multi.c/h` - Multi-threaded tests (10 tests including race conditions, concurrent stress)
- `slab_test_benchmark.c/h` - Performance benchmarks comparing slab vs object pool
- Tests run automatically during kernel boot in `main()`

## Key Design Patterns

### Adding a New System Call

1. Add syscall number to `kernel/syscall.h`
2. Add implementation in `kernel/sysproc.c` or `kernel/sysfile.c`
3. Add entry to `syscalls[]` array in `kernel/syscall.c`
4. Add stub generation to `user/usys.pl`
5. Add declaration to `user/user.h`

### Memory Allocation Strategy

- **Kernel page allocation**: Use `kalloc()` for 4KB pages, `kfree()` to release
- **Kernel object allocation**: Use slab allocator for fixed-size objects
  - Create cache with `kmem_cache_create()`
  - Allocate with `kmem_cache_alloc()`, free with `kmem_cache_free()`
  - Destroy cache with `kmem_cache_destroy()`
- **User heap allocation**: User programs use `malloc()` from `user/umalloc.c`

### Synchronization Rules

- Use `acquire()` and `release()` for spinlocks (short critical sections)
- Use `acquiresleep()` and `releasesleep()` for sleep locks (long operations)
- Always acquire locks in consistent order to prevent deadlock
- Never hold spinlock across context switches or I/O operations

## Important Constants

Defined in `kernel/param.h`:
- `NPROC 64` - Maximum processes
- `NCPU 8` - Maximum CPUs
- `NOFILE 16` - Open files per process
- `PGSIZE 4096` - Page size (from `kernel/riscv.h`)
- `BSIZE 1024` - File system block size (from `kernel/fs.h`)

## RISC-V Architecture Notes

- Runs in supervisor mode (S-mode) with page tables enabled
- Uses RISC-V sv39 paging (3-level page tables)
- Trap handling uses `trampoline.S` for user/kernel transitions
- Context switches preserve callee-saved registers (s0-s11, sp, ra)
- Machine mode code only in `kernel/start.c` for early initialization
