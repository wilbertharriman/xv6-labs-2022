// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define MAXPAGES ((PHYSTOP-KERNBASE)/PGSIZE)
#define PA2IDX(pa) ((pa-KERNBASE)/PGSIZE)

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  int refcnt[MAXPAGES];
} pagerefcnt;

int
getpageref_nolock(uint64 pa)
{
  // make sure lock is acquired before calling
  int idx = PA2IDX(pa);
  if (idx < 0 || idx >= MAXPAGES)
    panic("getpageref: pa out of bounds");
  return pagerefcnt.refcnt[idx];
}

void
setpageref(uint64 pa, int val)
{
  int idx = PA2IDX(pa);
  if (idx < 0 || idx >= MAXPAGES)
    return;
  acquire(&pagerefcnt.lock);
  pagerefcnt.refcnt[idx] = val;
  release(&pagerefcnt.lock);
}

void
pagerefincr(uint64 pa)
{
  int idx = PA2IDX(pa);
  if (idx < 0 || idx >= MAXPAGES)
    return;
  acquire(&pagerefcnt.lock);
  ++pagerefcnt.refcnt[idx];
  release(&pagerefcnt.lock);
}

void
pagerefdecr(uint64 pa)
{
  int idx = PA2IDX(pa);
  if (idx < 0 || idx >= MAXPAGES)
    return;
  acquire(&pagerefcnt.lock);
  --pagerefcnt.refcnt[idx];
  release(&pagerefcnt.lock);
}

void
rfinit()
{
  initlock(&pagerefcnt.lock, "pagerefcnt");
  memset(pagerefcnt.refcnt, 0, sizeof(pagerefcnt.refcnt));
}

void
kinit()
{
  rfinit();
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  uint64 pa64 = (uint64)pa;
  pagerefdecr(pa64);
  int idx = PA2IDX(pa64);
  acquire(&pagerefcnt.lock);
  if (idx >= 0 && idx < MAXPAGES && getpageref_nolock(pa64) > 0){
    release(&pagerefcnt.lock);
    return;
  }
  release(&pagerefcnt.lock);
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    setpageref((uint64)r, 1);
  }
  
  return (void*)r;
}
