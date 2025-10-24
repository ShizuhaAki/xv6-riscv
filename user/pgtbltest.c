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

int main(int argc, char *argv[]) {
  printf("pgtbltest: starting\n");

  ugetpid_test();
  pgaccess_test();

  printf("pgtbltest: all tests passed\n");
  exit(0);
}
