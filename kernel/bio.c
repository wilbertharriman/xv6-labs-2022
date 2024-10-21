// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NUM_BUCKET 13
char bucket_names[NUM_BUCKET][30];

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
} bcache;

struct {
  struct spinlock lock;

  // Linked list of buffers that are hashed to this bucket.
  struct buf head;
} bucket[NUM_BUCKET];

void
binit(void)
{
  initlock(&bcache.lock, "bcache");
  for (int i = 0; i < NUM_BUCKET; i++) {
    snprintf(bucket_names[i], sizeof(bucket_names[i]), "bcache.bucket%d", i);
    initlock(&bucket[i].lock, bucket_names[i]);
    bucket[i].head.prev = &bucket[i].head;
    bucket[i].head.next= &bucket[i].head;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  uint hash = (dev + blockno) % NUM_BUCKET;
  acquire(&bucket[hash].lock);

  // Is the block already cached?
  struct buf *b;
  for(b = bucket[hash].head.next; b != &bucket[hash].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bucket[hash].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bucket[hash].lock);

  // Not cached.
  // Recycle the unused buffer.
  acquire(&bcache.lock);
  acquire(&bucket[hash].lock);

  // Check again to see if someone else has brought in the page to cache
  for(b = bucket[hash].head.next; b != &bucket[hash].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      release(&bucket[hash].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  for (int i = 0; i < NBUF; i++) {
    struct buf *b = &bcache.buf[i];
    uint prev_hash = (b->dev + b->blockno) % NUM_BUCKET;
    if (prev_hash != hash)
      acquire(&bucket[prev_hash].lock);
    if (b->refcnt == 0) {
      if (b->prev)
        b->prev->next = b->next;
      if (b->next)
        b->next->prev = b->prev;
      if (prev_hash != hash)
        release(&bucket[prev_hash].lock);
      b->next = bucket[hash].head.next;
      b->prev = &bucket[hash].head;
      bucket[hash].head.next->prev = b;
      bucket[hash].head.next = b;

      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;

      release(&bcache.lock);
      release(&bucket[hash].lock);
      acquiresleep(&b->lock);
      return b;
    }
    if (prev_hash != hash)
      release(&bucket[prev_hash].lock);
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint hash = (b->dev + b->blockno) % NUM_BUCKET;
  acquire(&bucket[hash].lock);
  b->refcnt--;
  release(&bucket[hash].lock);
}

void
bpin(struct buf *b) {
  uint hash = (b->dev + b->blockno) % NUM_BUCKET;
  acquire(&bucket[hash].lock);
  b->refcnt++;
  release(&bucket[hash].lock);
}

void
bunpin(struct buf *b) {
  uint hash = (b->dev + b->blockno) % NUM_BUCKET;
  acquire(&bucket[hash].lock);
  b->refcnt--;
  release(&bucket[hash].lock);
}


