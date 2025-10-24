# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is xv6-riscv, a re-implementation of Unix Version 6 for modern RISC-V multiprocessors using ANSI C. It's a teaching operating system used for MIT's 6.1810 course.

### Course Information
- **Course**: Operating Systems
- **Student**: Zecyel (朱程炀)
- **Student ID**: 23300240014
- **Email**: i@zecyel.xyz

### Custom Extensions
This codebase has been extended with the following custom implementations:

**Lab 0: Slab Memory Allocator** (Completed 2025-09-25)
- Linux-style slab allocator for efficient kernel memory management
- Comprehensive test suite with 29 tests (single-threaded, multi-threaded, benchmarks)
- Performance: 2608% throughput improvement over object pool allocator
- Memory utilization: 95.90% (vs 53.89% for object pool)
- Documentation: `doc/lab0.md`

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

The project uses Google style with custom settings in `.clang-format`. See `doc/lab-1.md` for detailed setup instructions.

### Testing
```bash
# Kernel boots and runs all slab allocator tests automatically
make qemu

# Run specific test suites (modify kernel/main.c to enable/disable):
# - Single-threaded tests: kernel/test/slab_test_single.c (10 tests)
# - Multi-threaded tests: kernel/test/slab_test_multi.c (10 tests)
# - Benchmarks: kernel/test/slab_test_benchmark.c (slab vs object pool)
```

**Test Coverage** (29 total tests):
- Basic allocation/deallocation
- Batch operations (unaligned, large, huge batches)
- Memory alignment verification
- Constructor/destructor callbacks
- Memory integrity and corruption detection
- Edge cases and boundary conditions
- Race conditions and concurrent access
- Multi-cache management
- Stress testing and fragmentation analysis
- Performance benchmarks

### Environment Setup
See `doc/lab-1.md` for detailed environment setup including:
- Installing and configuring clang-format-20
- Setting up RISC-V toolchain
- QEMU installation and configuration

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

**Test Results Summary**:
All 29 tests pass with the following performance metrics:

| Metric              | Object Pool | Slab Allocator | Improvement   |
| ------------------- | ----------- | -------------- | ------------- |
| Throughput (ops/s)  | 59,773      | 1,619,083      | +2608%        |
| Delay (cycles)      | 167         | 6              | -96.40%       |
| Memory Utilization  | 53.89%      | 95.90%         | +78% (42pts)  |

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

## Development Workflow

### Code Quality Standards
1. **Formatting**: All code must follow Google C style (enforced via clang-format)
2. **Testing**: New memory allocator features must include comprehensive tests
3. **Documentation**: Update relevant `.md` files in `doc/` for major changes
4. **Comments**: Use clear, concise comments explaining non-obvious logic

### Adding New Features
1. Plan implementation in `doc/lab-N.md`
2. Implement core functionality in appropriate `kernel/` file
3. Add tests in `kernel/test/` if applicable
4. Update `kernel/main.c` to run new tests during boot
5. Document results and performance metrics
6. Format all modified files with clang-format

### Best Practices
- **Memory Safety**: Always check return values from allocators
- **Concurrency**: Use appropriate locks (spinlock vs sleeplock)
- **Resource Cleanup**: Free all allocated resources on error paths
- **Testing**: Test both single-threaded and multi-threaded scenarios
- **Performance**: Benchmark against baseline implementations

## Troubleshooting

### Common Build Issues
```bash
# If toolchain not found:
export PATH=$PATH:/path/to/riscv64-unknown-elf-gcc/bin

# If QEMU not found:
which qemu-system-riscv64  # Should be in PATH

# Clean rebuild:
make clean && make qemu
```

### Debugging Tips
```bash
# Enable GDB debugging:
# Terminal 1:
make qemu-gdb

# Terminal 2:
riscv64-unknown-elf-gdb kernel/kernel
(gdb) target remote :(port_from_qemu_output)
(gdb) b main
(gdb) c
```

### Test Failures
- Check `kernel/main.c` for test execution order
- Review test output for specific failure details
- Use `printf()` debugging in kernel code
- Verify memory allocator state with debug prints

## Project Structure

```
xv6-riscv/
├── kernel/              # Kernel source code
│   ├── test/           # Test suites (slab allocator tests)
│   ├── main.c          # Kernel initialization and test runner
│   ├── kalloc.c/h      # Page allocator
│   ├── slab.c/h        # Slab allocator (custom)
│   ├── proc.c/h        # Process management
│   ├── fs.c/h          # File system
│   └── ...
├── user/               # User programs and utilities
├── doc/                # Lab documentation
│   ├── lab0.md        # Slab allocator documentation
│   └── lab-1.md       # Environment setup guide
├── .clang-format       # Code formatting rules
├── Makefile           # Build configuration
├── CLAUDE.md          # This file
└── README             # Original xv6 README
```

## Lab Assignments

### Lab 0: Slab Memory Allocator ✅
**Status**: Completed (2025-09-25)

**Implementation**:
- Linux-style slab allocator with three-list design (partial/full/empty)
- Object-level caching with optional constructor/destructor callbacks
- Thread-safe with per-cache spinlocks
- Efficient memory utilization (~96%) and high performance (2608% faster)

**Files Modified/Added**:
- `kernel/slab.c/h` - Core slab allocator implementation
- `kernel/test/slab_test_single.c/h` - Single-threaded test suite
- `kernel/test/slab_test_multi.c/h` - Multi-threaded test suite
- `kernel/test/slab_test_benchmark.c/h` - Performance benchmarks
- `kernel/main.c` - Test runner integration
- `doc/lab0.md` - Lab documentation and results

**Key Achievements**:
- All 29 tests passing (fuzz, stress, safety tests)
- 27x throughput improvement over object pool
- 96% reduction in allocation latency
- 42 percentage point improvement in memory utilization

### Future Labs
Additional labs and features will be documented here as they are implemented.

### Lab 1: Speed Up System Calls ✅
**Status**: Completed (2025-10-24)

**Implementation**:
- Fast path for `getpid()` system call using shared read-only memory
- Eliminates kernel crossing overhead for PID queries
- Read-only page mapping (USYSCALL) in user address space
- 25-500x performance improvement over traditional syscall

**Files Modified/Added**:
- `kernel/memlayout.h` - Added USYSCALL definition and struct usyscall
- `kernel/proc.h` - Added usyscall pointer to process struct
- `kernel/proc.c` - Lifecycle management for USYSCALL page (allocation, mapping, cleanup)
- `user/pgtbltest.c` - Test program with ugetpid() fast path implementation
- `Makefile` - Added pgtbltest to user programs
- `doc/lab1.md` - Lab documentation and analysis

**Key Achievements**:
- Zero-overhead PID queries (no kernel crossing)
- Read-only protection prevents user-space tampering
- Follows xv6 trapframe lifecycle pattern
- Minimal memory overhead (4KB per process)
- Demonstrates Linux vDSO-like optimization technique

**Technical Details**:
- Memory layout: `USYSCALL` page placed between user heap and `TRAPFRAME`
- Permissions: `PTE_R | PTE_U` (read-only, user-accessible)
- Initialization: PID written once during process creation
- Security: Write-protected to prevent PID spoofing

## References

- [xv6 Book](https://pdos.csail.mit.edu/6.1810/2024/xv6/book-riscv-rev4.pdf)
- [MIT 6.1810 Course](https://pdos.csail.mit.edu/6.1810/)
- [RISC-V Specifications](https://riscv.org/specifications/)
- [Linux Slab Allocator](https://www.kernel.org/doc/html/latest/core-api/mm-api.html#slab-allocators)

## License and Acknowledgments

xv6 is inspired by Unix Version 6 and developed by MIT CSAIL. See `README` file for full acknowledgments.

**Custom Extensions**: Zecyel (朱程炀), 2025
