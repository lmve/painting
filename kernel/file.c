/*
 * 逻辑文件层
*/

#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fat32.h"
#include "file.h"
#include "stat.h"
#include "proc.h"
#include "defs.h"

/* 定义了一个设备切换表，用于管理系统的设备驱动程序 */
struct devsw devsw[NDEV];
struct {
  struct spinlock lock;  /* 用于文件表的自旋锁，保证并发访问的安全性 */
  struct file file[NFILE]; /* 文件表，存储系统中打开的文件信息 */
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
  struct file *f;
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    memset(f, 0, sizeof(struct file));
  }
  /* for test */
  printf("fileinit\n");
  
}

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
  return NULL;
}

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
    // pipeclose(ff.pipe, ff.writable);
    panic("pipe is unsuport\n");
  } else if(ff.type == FD_ENTRY){
    eput(ff.ep);
  } else if (ff.type == FD_DEVICE) {

  }
}

/* 将获取的文件状态信息复制到指定的内存地址中 */
int
filestat(struct file *f, uint64 addr)
{
  // struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_ENTRY){
    elock(f->ep);
    estat(f->ep, &st);
    eunlock(f->ep);
    // if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
    if(copyout2(addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  switch (f->type) {
    case FD_PIPE:
        // r = piperead(f->pipe, addr, n);
        break;
    case FD_DEVICE:
        if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
          return -1;
        r = devsw[f->major].read(1, addr, n);
        break;
    case FD_ENTRY:
        elock(f->ep);
          if((r = eread(f->ep, 1, addr, f->off, n)) > 0)
            f->off += r;
        eunlock(f->ep);
        break;
    default:
      panic("fileread");
  }

  return r;
}

int
filewrite(struct file *f, uint64 addr, int n)
{
  int ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    // ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_ENTRY){
    elock(f->ep);
    if (ewrite(f->ep, 1, addr, f->off, n) == n) {
      ret = n;
      f->off += n;
    } else {
      ret = -1;
    }
    eunlock(f->ep);
  } else {
    panic("filewrite");
  }

  return ret;
}

int
dirnext(struct file *f, uint64 addr)
{
  // struct proc *p = myproc();

  if(f->readable == 0 || !(f->ep->attribute & ATTR_DIRECTORY))
    return -1;

  struct dirent de;
  struct stat st;
  int count = 0;
  int ret;
  elock(f->ep);
  while ((ret = enext(f->ep, &de, f->off, &count)) == 0) {  // skip empty entry
    f->off += count * 32;
  }
  eunlock(f->ep);
  if (ret == -1)
    return 0;

  f->off += count * 32;
  estat(&de, &st);
  // if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
  if(copyout2(addr, (char *)&st, sizeof(st)) < 0)
    return -1;

  return 1;
}