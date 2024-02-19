#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char *path, char *filename) {
  char buf[512], *p;
  int fd;
  struct stat st;
  struct dirent de;

  if ((fd = open(path, 0)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type) {
  case T_DEVICE:
  case T_FILE:
    for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
    p++;
    if (strcmp(p, filename) == 0)
      printf("%s\n", path);
    break;
  case T_DIR:
    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
      printf("find: path is too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
      if (de.inum == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
        continue;
      find(buf, filename);
    }
    break;
  }
  close(fd);
}

int main(int argc, char *argv[]) {
  if(argc != 3) {
    printf("find: usage find [PATH] [FILE]\n");
    exit(0);
  }

  find(argv[1], argv[2]);
  exit(0);
}