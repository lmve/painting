/*
 * 将底层的 block 进行改造 基于扇区的 cache
 * * * * * * * * * * * * * * * * * * * * * 
 * 在此修改了 BSIZE 
 * buf

*/

#ifndef _BUF_H_
#define _BUF_H_

struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?,  表示该缓冲区的数据已经写到磁盘, 缓冲区中的内容可能会发生变化
  uint dev;
  uint sectorno;
  struct sleeplock lock;
  uint refcnt;      // 引用次数
  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
};


#endif