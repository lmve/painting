#ifndef __FILE_H__
#define __FILE_H__

/*
 * 逻辑文件数据定义 
*/
struct file {
  enum { FD_NONE, FD_PIPE, FD_ENTRY, FD_DEVICE } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE
  struct dirent *ep;
  uint off;          // FD_ENTRY
  short major;       // FD_DEVICE
};

struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];


#define CONSOLE 1   // 控制台设备标识符

#endif // !__FILE_H__
