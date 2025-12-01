#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define SIGINT 2  // Signal number for test

volatile int signal_received = 0;

// Signal handler function
void sighandler(int signum) {
  printf("sigtest: caught signal %d\n", signum);
  signal_received = 1;
  // Call sigreturn to restore the interrupted context
  sigreturn();
}

int main(int argc, char *argv[]) {
  int pid;

  printf("sigtest: starting test...\n");

  pid = fork();

  if (pid == 0) {
    // Child process: register signal handler and wait
    signal(SIGINT, sighandler);
    printf("sigtest (child %d): waiting for signal...\n", getpid());

    // Spin until signal is received
    while (!signal_received) {
      // Keep running, waiting for signal interrupt
    }

    printf("sigtest (child %d): signal handler returned, exiting\n", getpid());
    exit(0);
  } else if (pid > 0) {
    // Parent process: wait a bit, then send signal to child
    pause(50);  // Wait 50 ticks (about 5 seconds)
    printf("sigtest (parent %d): sending signal %d to child %d\n", getpid(), SIGINT, pid);
    kill(pid, SIGINT);  // Send signal

    int status;
    wait(&status);
    printf("sigtest (parent): child exited with status %d\n", status);
    printf("sigtest: TEST PASSED\n");
  } else {
    printf("sigtest: fork failed\n");
    exit(1);
  }

  exit(0);
}
