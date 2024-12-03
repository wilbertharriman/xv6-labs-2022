//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"
#include "fcntl.h"

struct devsw devsw[NDEV];

struct {
  struct spinlock lock;
  struct vma vma[NVMA];
} vmatable;

struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
vmainit(void)
{
  initlock(&vmatable.lock, "vmatable");
}

struct vma*
vmaalloc(void)
{
  struct vma *v;
  acquire(&vmatable.lock);
  for(v = vmatable.vma; v < vmatable.vma + NVMA; v++) {
    if(v->used == 0) {
      v->used = 1;
      release (&vmatable.lock);
      return v;
    }
  }
  release(&vmatable.lock);
  return 0;
}

int
vmaattach(struct vma *v)
{
  int i;
  struct proc *p = myproc();
  for (i = 0; i < NVMA; i++){
    if (p->vmas[i] == 0){
      p->vmas[i] = v;
      return i;
    }
  }
  return -1;
}

void
vmarelease(struct vma *v)
{
  acquire(&vmatable.lock);
  if (v->used == 0) {
    panic("vmarelease");
  }
  v->used = 0;
  release(&vmatable.lock);
}

int
vmalookup(uint64 va)
{
  struct proc *p = myproc();
  struct vma *v;
  for (int i = 0; i < NVMA; i++) {
    v = p->vmas[i];
    if (v == 0) continue;
    if (va >= v->start && va < v->end)
      return i;
  }
  return -1;
}

int munmap(struct vma *v, uint64 addr, int len)
{
  int free = 0;
  struct proc *p = myproc();
  if (v->flags == MAP_SHARED) {
    // write starting from [addr:addr+len] to file at offset (addr - v->start)
    int r;
    int i = 0;
    uint off = v->offset + (addr - v->start);
    int write_len = len < (v->f->ip->size - off) ? len : (v->f->ip->size - off);
    while (i < write_len) {
      int n1 = write_len - i;
      if (n1 > PGSIZE)
        n1 = PGSIZE;
      pte_t* pte = walk(p->pagetable, addr + i, 0);
      if (*pte & PTE_V) {
        begin_op();
        ilock(v->f->ip);
        if ((r = writei(v->f->ip, 1, addr + i, off, n1)) > 0)
          off += r;
        iunlock(v->f->ip);
        end_op();
        if (r != n1) {
          panic("failed writing to file");
        }
      }
      i += n1;
    }
  }

  uint64 startva, endva;

  if (addr == v->start) {
    if (addr + len == v->end) {
      startva = v->start;
      endva = PGROUNDUP(v->end);
      fileclose(v->f);
      vmarelease(v);
      free = 1;
    } else {
      startva = v->start;
      endva = PGROUNDDOWN(v->start + len);
      v->start += len;
      v->offset += endva - startva;
    }
  } else if (addr + len == v->end) {
    startva = PGROUNDUP(addr);
    endva = PGROUNDUP(v->end);
    v->end -= len;
  } else {
    panic("munmap assumes will not punch a hole in the middle of mmap region");
  }
  uvmunmap(p->pagetable, startva, (endva-startva)/PGSIZE, 1);
  return free;
}

int
handle_page_fault(uint64 va) {
  struct vma* v;
  char *mem;
  int idx;
  if ((idx = vmalookup(va)) == -1)
    return -1;
  v = myproc()->vmas[idx];
  mem = kalloc();
  ilock(v->f->ip);
  readi(v->f->ip, 0, (uint64)mem, v->offset + (va - v->start), PGSIZE);
  iunlock(v->f->ip);
  // call mappages to map page to va
  if (mappages(myproc()->pagetable, va, PGSIZE, (uint64)mem, v->perm | PTE_U) != 0) {
    kfree(mem);
    return -1;
  }
  return 0;
}

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE){
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

// Write to file f.
// addr is a user virtual address.
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r != n1){
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}

