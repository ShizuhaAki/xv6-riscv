#include "kernel/stat.h"
#include "kernel/types.h"
#include "user/user.h"

#define SIGKILL 9  // Default signal for termination

int main(int argc, char **argv) {
  int i;

  if (argc < 2) {
    fprintf(2, "usage: kill pid...\n");
    exit(1);
  }
  for (i = 1; i < argc; i++) kill(atoi(argv[i]), SIGKILL);
  exit(0);
}
