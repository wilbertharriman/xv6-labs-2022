// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

char names[NCPU][6];

struct run {
  struct run *next;
};

struct kmem {
  struct spinlock lock;
  struct run *freelist;
};

struct kmem kmem[NCPU];

uint get_cpuid() {
  push_off();
  uint ret = cpuid();
  pop_off();
  return ret;
}

void
kinit()
{
  for (int i = 0; i < NCPU; ++i) {
    snprintf(names[i], sizeof(names[i]), "kmem%d", i);
    initlock(&kmem[i].lock, names[i]);
  }
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

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  uint idx = get_cpuid();
  acquire(&kmem[idx].lock);
  r->next = kmem[idx].freelist;
  kmem[idx].freelist = r;
  release(&kmem[idx].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  uint cur_idx = get_cpuid();

  acquire(&kmem[cur_idx].lock);
  r = kmem[cur_idx].freelist;
  if (r)
    kmem[cur_idx].freelist = r->next;
  release(&kmem[cur_idx].lock);

  // stealing
  if (!r) {
    uint idx = (cur_idx + 1) % NCPU;
    while (idx != cur_idx) {
      acquire(&kmem[idx].lock);
      r = kmem[idx].freelist;
      if (r) {
        kmem[idx].freelist = r->next;
        release(&kmem[idx].lock);
        break;
      }
      release(&kmem[idx].lock);
      idx = (idx + 1) % NCPU;
    }
  }
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
