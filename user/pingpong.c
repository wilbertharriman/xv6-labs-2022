#include "kernel/types.h"
#include "user/user.h"

int main() {
  int p[2];
  if(pipe(p) < 0)
    fprintf(2, "pipe");
  int pid = fork();
  if (pid == -1)
    fprintf(2, "fork");

  if (pid == 0) {
    read(p[0], (void*)0, 1);
    printf("%d: received ping\n", getpid());
    write(p[1], " ", 1);
  } else {
    write(p[1], " ", 1);
    read(p[0], (void*)0, 1);
    printf("%d: received pong\n", getpid());
  }
  exit(0);
}