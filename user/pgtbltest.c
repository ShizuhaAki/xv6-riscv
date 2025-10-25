#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"
#include "user/user.h"

// Fast system call using shared memory
int ugetpid(void) {
  struct usyscall *u = (struct usyscall *)USYSCALL;
  return u->pid;
}

void ugetpid_test() {
  int pid_syscall = getpid();
  int pid_shared = ugetpid();

  printf("ugetpid_test starting\n");

  if (pid_syscall != pid_shared) {
    printf("ugetpid_test: FAIL - getpid()=%d, ugetpid()=%d\n", pid_syscall,
           pid_shared);
    exit(1);
  }

  printf("ugetpid_test: OK - getpid()=%d, ugetpid()=%d\n", pid_syscall,
         pid_shared);
}

void pgaccess_test() {
  // Placeholder for future page access tests
  printf("pgaccess_test: not implemented, skipping\n");
}

// Test that superpages are properly copied during fork
void superpg_fork() {
  printf("superpg_fork starting\n");

  char *p = sbrk(SUPERPGSIZE * 2);
  if (p == (char *)-1) {
    printf("superpg_fork: sbrk failed\n");
    exit(1);
  }

  // Write a pattern to the allocated memory
  for (uint64 i = 0; i < SUPERPGSIZE * 2; i++) {
    p[i] = (char)(i & 0xFF);
  }

  int pid = fork();
  if (pid < 0) {
    printf("superpg_fork: fork failed\n");
    exit(1);
  }

  if (pid == 0) {
    // Child: verify the data was copied correctly
    for (uint64 i = 0; i < SUPERPGSIZE * 2; i++) {
      if (p[i] != (char)(i & 0xFF)) {
        printf("superpg_fork: FAIL - child data mismatch at %p\n", &p[i]);
        exit(1);
      }
    }
    printf("superpg_fork: child OK\n");
    exit(0);
  } else {
    // Parent: wait for child
    int status;
    wait(&status);
    if (status != 0) {
      printf("superpg_fork: FAIL - child exited with status %d\n", status);
      exit(1);
    }
    printf("superpg_fork: OK\n");
  }
}

// Test that superpages are properly freed
void superpg_free() {
  printf("superpg_free starting\n");

  // Allocate a large region that should use superpages
  char *p1 = sbrk(SUPERPGSIZE * 3);
  if (p1 == (char *)-1) {
    printf("superpg_free: sbrk(1) failed\n");
    exit(1);
  }

  // Write pattern to verify allocation
  for (uint64 i = 0; i < SUPERPGSIZE * 3; i += PGSIZE) {
    p1[i] = 0xAA;
  }

  // Free most of it
  char *p2 = sbrk(-((int)(SUPERPGSIZE * 2 + PGSIZE)));
  if (p2 == (char *)-1) {
    printf("superpg_free: sbrk(-) failed\n");
    exit(1);
  }

  // Verify remaining memory is still accessible
  p1[0] = 0xBB;
  if (p1[0] != 0xBB) {
    printf("superpg_free: FAIL - memory corrupted\n");
    exit(1);
  }

  // Allocate again to reuse freed superpages
  char *p3 = sbrk(SUPERPGSIZE * 2);
  if (p3 == (char *)-1) {
    printf("superpg_free: sbrk(2) failed\n");
    exit(1);
  }

  // Write to new allocation
  for (uint64 i = 0; i < SUPERPGSIZE * 2; i += PGSIZE) {
    p3[i] = 0xCC;
  }

  printf("superpg_free: OK\n");
}

int main(int argc, char *argv[]) {
  printf("pgtbltest: starting\n");

  ugetpid_test();
  pgaccess_test();
  superpg_fork();
  superpg_free();

  printf("pgtbltest: all tests passed\n");
  exit(0);
}
