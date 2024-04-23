/*
 * 磁盘读写
*/

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "buf.h"
#include "virtio.h"
#include "defs.h"


void disk_init(void)
{
    devinit();
}
void disk_read(struct buf *b)
{
    
	Virtioread(b,b->sectorno);
}
void disk_write(struct buf *b)
{
	Virtiowrite(b, b->sectorno);
}

void disk_intr(void)
{
    virtiointr();
}