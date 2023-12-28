#include "kernel/types.h"
#include "user/user.h"

int main() {
  int p[2];
  if (pipe(p) < 0) {
    fprintf(2, "pipe error");
    exit(1);
  }

  if(fork() == 0) {
    close(p[1]);
    int p1[2];
    int prime; 
    read(p[0], &prime, sizeof(int));
    printf("prime %d\n", prime);
    int i;
    uint8 forked = 0;
    while (read(p[0], &i, sizeof(int)) == sizeof(int)) {
      if(i % prime == 0) continue;
      if (forked) {
        write(p1[1], &i, sizeof(int));
        continue;
      }
      // else
      pipe(p1);
      if(fork() == 0) {
        close(p1[1]);
        close(p[0]);
        p[0] = p1[0];
        p[1] = p1[1];

        read(p[0], &prime, sizeof(int));
        printf("prime %d\n", prime);
        continue;
      } else {
        close(p1[0]);
        forked = 1;
        write(p1[1], &i, sizeof(int));
      }
    }
    close(p1[1]);
    wait(0);
  } else {
    close(p[0]);
    for (int i = 2; i < 55; ++i) {
      write(p[1], &i, sizeof(int));
    }
    close(p[1]);
    wait(0);
  }
  exit(0);
}
