/*
 * buffer cache 层
 * 主要修改了底层磁盘缓冲块 以扇区为缓存单位
*/

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "buf.h"
#include "riscv.h"
#include "defs.h"


/* 缓冲区为一个双向链表 */
struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
} bcache;

/* 
 * * * * * * * * * * * * * * * * * * * * * * 
 * 初始化缓冲区
*/
void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->refcnt = 0;
    b->sectorno = ~0;
    b->dev = ~0;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  /* for test */
  printf("binit\n");
  // printf("%d\n",bread(0,0));

}

// 使用LRU算法, 替换掉最少访问的缓存块
// 寻找一个缓存块给设备号为dev的设备
// 如果没有找到就分配一个
// 无论哪种情况,都返回一个locked buffer
/*
 * * * * * * * * * * * * * * * * * * * * * *
 * 基于 cache 的读写
 * disk_rw 
*/
static struct buf*
bget(uint dev, uint sectorno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->sectorno == sectorno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleeplock(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    printf("[into bget] \n");
    printf("b-refcnt = %d \n",b->refcnt);
    printf("b->dev = %d\n",b->dev);
    printf("b->sectorno = %d\n",b->sectorno);

    if(b->refcnt != 0) {
        continue;
    } else {
      b->valid = 0;
      b->dev = dev;
      b->sectorno = sectorno;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleeplock(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
  /* somthing is wrong */
  b = NULL;
  return b;
}
/*
 * * * * * * * * * * * * * * * * * * * * * *
 * 释放一个缓冲块
*/
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleeplock(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}

struct buf* 
bread(uint dev, uint sectorno) {
  struct buf *b;
  b = NULL;
  b = bget(dev, sectorno);
  printf("[into bread] \n");
  if (!b->valid) {
    /* for test */
    printf("[into disk_read] \n");
    disk_read(b);
    b->valid = 1;
  }

  return b;
}

// Write b's contents to disk.  Must be locked.
void 
bwrite(struct buf *b) {
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  disk_write(b);
}

/*
 * 引用计数
*/
void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}
