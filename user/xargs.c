#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("xargs: argument n < 2\n");
    exit(0);
  }

  int j = 0;
  char *new_argv[MAXARG];
  for (int i = 1; i < argc; i++) {
    new_argv[j++] = argv[i];
  }

  char buf[512], *p;
  char c;
  p = buf;
  while(read(0, &c, 1) > 0) {
    if (c == '\n') {
      *p++ = 0;
      if (fork() == 0) {
        new_argv[j] = buf;
        exec(new_argv[0], new_argv);
      }
      wait(0);
      p = buf;
    } else {
      *p++ = c;
    }
  }
  exit(0);
}
