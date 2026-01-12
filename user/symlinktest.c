#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

static void
basic(void)
{
  // printf("Entering Basic\n");
  int fd = open("real", O_CREATE | O_WRONLY);
  // printf("Opened file.\n");
  if (fd < 0) { printf("symlinktest: open real failed\n"); exit(1); }
  write(fd, "hello", 5);
  // printf("Written\n");
  close(fd);
  // printf("Closed\n");

  if (symlink("real", "link") < 0) { printf("symlinktest: symlink failed\n"); exit(1); }

  // printf("Symlink checked\n");

  fd = open("link", O_RDONLY);
  if (fd < 0) { printf("symlinktest: open link failed\n"); exit(1); }
  char buf[6] = {0};
  read(fd, buf, 5);
  close(fd);

  if (strcmp(buf, "hello") != 0) {
    printf("symlinktest: expect hello, got %s\n", buf);
    exit(1);
  }
}

static void
dangling(void)
{
  if (symlink("no_such_target", "dangling") < 0) {
    printf("symlinktest: dangling create failed\n");
    exit(1);
  }
  int fd = open("dangling", O_RDONLY);
  if (fd >= 0) {
    printf("symlinktest: dangling open should fail\n");
    close(fd);
    exit(1);
  }
}

int
main(void)
{
  basic();
  // printf("114514\n");
  dangling();
  printf("symlinktest: ok\n");
  exit(0);
}