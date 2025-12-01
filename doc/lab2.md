# Lab 2: Signal Mechanism

**Student**: Zecyel (朱程炀)
**Student ID**: 23300240014
**Date**: 2025-12-02

## Overview

This lab implements a basic Unix-style signal mechanism for xv6. Signals provide a way for processes to handle asynchronous events, such as user interrupts or inter-process communication.

## Design

### Signal Model

The implementation follows a simplified Unix signal model:

1. **Signal Registration**: Processes register signal handlers using `signal(signum, handler)`
2. **Signal Delivery**: Signals are sent using `kill(pid, signum)`
3. **Signal Handling**: When a signal is pending, the kernel redirects execution to the handler before returning to user space
4. **Context Restoration**: After the handler completes, `sigreturn()` restores the original execution context

### Key Design Decisions

1. **Sentinel Value for Default Handler**: Since xv6 user programs are linked starting at address `0x0`, we cannot use `0` as a sentinel for "no handler". Instead, we use `-1` (`SIG_DFL`) to indicate that no handler is registered.

2. **Signal Checking Point**: Signals are checked in `usertrap()` just before returning to user space. This ensures signals are delivered on:
   - System call returns
   - Timer interrupts (preemption)
   - Page faults

3. **Non-reentrant Signal Handling**: While a signal handler is executing (`sig_tf_backup_valid` is set), no additional signals are dispatched. This prevents nested signal handling complexity.

4. **SIGKILL Special Handling**: Signal 9 (SIGKILL) cannot be caught or ignored - it always terminates the process.

## Implementation

### Data Structures

Added to `struct proc` in `kernel/proc.h`:

```c
// Signal-related fields
uint32 pending_signals;          // Bitmask of pending signals
void (*sig_handlers[NSIG])(int); // Signal handler function pointers
struct trapframe sig_tf_backup;  // Backup of trapframe for sigreturn
int sig_tf_backup_valid;         // Is sig_tf_backup valid?
```

Added to `kernel/param.h`:

```c
#define NSIG 32      // Maximum number of signals
#define SIGKILL 9    // Kill signal (cannot be caught)
```

### System Calls

#### `int signal(int signum, void (*handler)(int))`

Registers a signal handler for the specified signal number.

**Implementation** (`kernel/sysproc.c`):
```c
uint64 sys_signal(void) {
  int signum;
  uint64 handler;

  argint(0, &signum);
  argaddr(1, &handler);

  if (signum < 0 || signum >= NSIG) {
    return -1;
  }

  struct proc *p = myproc();
  p->sig_handlers[signum] = (void (*)(int))handler;

  return 0;
}
```

#### `int kill(int pid, int signum)`

Sends a signal to the specified process.

**Implementation** (`kernel/sysproc.c`):
```c
uint64 sys_kill(void) {
  int pid;
  int signum;

  argint(0, &pid);
  argint(1, &signum);

  if (signum < 0 || signum >= NSIG) {
    return -1;
  }

  // Find target process and set pending signal
  struct proc *p;
  extern struct proc proc[];

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->pid == pid) {
      p->pending_signals |= (1 << signum);
      if (p->state == SLEEPING) {
        p->state = RUNNABLE;  // Wake up to handle signal
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}
```

#### `int sigreturn(void)`

Restores the process context after signal handler execution.

**Implementation** (`kernel/sysproc.c`):
```c
uint64 sys_sigreturn(void) {
  struct proc *p = myproc();

  if (!p->sig_tf_backup_valid) {
    return -1;
  }

  *(p->trapframe) = p->sig_tf_backup;
  p->sig_tf_backup_valid = 0;

  return p->trapframe->a0;
}
```

**Special Handling in `syscall()`**: The `sigreturn` syscall must not overwrite `a0` after restoring the trapframe:

```c
void syscall(void) {
  // ...
  uint64 ret = syscalls[num]();
  // sigreturn restores the entire trapframe including a0
  if (num != SYS_sigreturn) {
    p->trapframe->a0 = ret;
  }
  // ...
}
```

### Signal Dispatch

Signal handling occurs in `handle_signals()` called from `usertrap()`:

```c
#define SIG_DFL ((void (*)(int))-1)

static int handle_signals(void) {
  struct proc *p = myproc();

  // Don't handle if already in a signal handler
  if (p->sig_tf_backup_valid) {
    return 0;
  }

  if (p->pending_signals == 0) {
    return 0;
  }

  for (int signum = 0; signum < NSIG; signum++) {
    if (p->pending_signals & (1 << signum)) {
      // SIGKILL always terminates
      if (signum == SIGKILL) {
        p->pending_signals &= ~(1 << signum);
        setkilled(p);
        return 0;
      }

      void (*handler)(int) = p->sig_handlers[signum];

      if (handler == SIG_DFL) {
        // No handler - default action: ignore
        p->pending_signals &= ~(1 << signum);
      } else {
        // Dispatch to user handler
        p->pending_signals &= ~(1 << signum);
        p->sig_tf_backup = *(p->trapframe);
        p->sig_tf_backup_valid = 1;
        p->trapframe->a0 = signum;
        p->trapframe->epc = (uint64)handler;
        return 1;
      }
    }
  }

  return 0;
}
```

### Process Lifecycle

**Initialization** (`allocproc()`):
- `pending_signals = 0`
- `sig_tf_backup_valid = 0`
- All handlers set to `SIG_DFL` (-1)

**Fork** (`kfork()`):
- Signal handlers are copied from parent to child
- Pending signals are NOT copied (child starts fresh)

**Exit** (`freeproc()`):
- All signal fields are cleared

## Testing

### Test Program (`user/sigtest.c`)

```c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define SIGINT 2

volatile int signal_received = 0;

void sighandler(int signum) {
  printf("sigtest: caught signal %d\n", signum);
  signal_received = 1;
  sigreturn();
}

int main(int argc, char *argv[]) {
  int pid;

  printf("sigtest: starting test...\n");

  pid = fork();

  if (pid == 0) {
    // Child: register handler and wait
    signal(SIGINT, sighandler);
    printf("sigtest (child %d): waiting for signal...\n", getpid());

    while (!signal_received) {
      pause(1);
    }

    printf("sigtest (child %d): signal handler returned, exiting\n", getpid());
    exit(0);
  } else if (pid > 0) {
    // Parent: send signal after delay
    pause(50);
    printf("sigtest (parent %d): sending signal %d to child %d\n",
           getpid(), SIGINT, pid);
    kill(pid, SIGINT);

    int status;
    wait(&status);
    printf("sigtest (parent): child exited with status %d\n", status);
    printf("sigtest: TEST PASSED\n");
  }

  exit(0);
}
```

### Test Output

```
$ sigtest
sigtest: starting test...
sigtest (child 4): waiting for signal...
sigtest (parent 3): sending signal 2 to child 4
sigtest: caught signal 2
sigtest (child 4): signal handler returned, exiting
sigtest (parent): child exited with status 0
sigtest: TEST PASSED
```

## Files Modified

| File | Changes |
|------|---------|
| `kernel/param.h` | Added `NSIG` and `SIGKILL` constants |
| `kernel/proc.h` | Added signal fields to `struct proc` |
| `kernel/proc.c` | Initialize/clear signal fields in lifecycle functions |
| `kernel/syscall.h` | Added `SYS_signal` and `SYS_sigreturn` |
| `kernel/syscall.c` | Added syscall handlers, special case for `sigreturn` |
| `kernel/sysproc.c` | Implemented `sys_signal`, modified `sys_kill`, added `sys_sigreturn` |
| `kernel/trap.c` | Added `handle_signals()` and call site in `usertrap()` |
| `user/user.h` | Updated `kill()` signature, added `signal()` and `sigreturn()` |
| `user/usys.pl` | Added syscall stubs |
| `user/kill.c` | Updated to use new `kill(pid, signum)` signature |
| `user/usertests.c` | Updated `kill()` calls |
| `user/grind.c` | Updated `kill()` calls |
| `user/sigtest.c` | New test program |
| `Makefile` | Added `sigtest` to user programs |

## Limitations

1. **Single pending signal per type**: Only one instance of each signal can be pending at a time (using a bitmask)

2. **No signal queuing**: If the same signal is sent multiple times before being handled, only one instance is recorded

3. **No signal masking**: Cannot block signals during critical sections (except implicitly during handler execution)

4. **Limited default actions**: All signals except SIGKILL have the same default action (ignore)

5. **No `SA_RESTART`**: System calls interrupted by signals are not automatically restarted

## Future Improvements

1. **Signal queuing** using a linked list instead of bitmask
2. **Signal masking** with `sigprocmask()`
3. **Real-time signals** with data payload
4. **`sigaction()`** for more control over signal handling
5. **Proper default actions** (terminate, stop, continue) for different signals

## References

- [Linux Signals](https://www.ic.unicamp.br/~celio/mc514/linux/linux_pgsignals.html)
- [xv6 Book - Chapter 4: Traps and system calls](https://pdos.csail.mit.edu/6.1810/2024/xv6/book-riscv-rev4.pdf)
- POSIX signal specification
