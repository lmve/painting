/*
 * virtio_disk.c 虚拟磁盘驱动程序
*/
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "buf.h"
#include "virtio.h"
#include "proc.h"
#include "defs.h"

// 私有结构体
static struct disk {
  char pages[2 * PGSIZE];
  
  // a set (not a ring) of DMA descriptors, with which the
  // driver tells the device where to read and write individual
  // disk operations. there are NUM descriptors.
  // most commands consist of a "chain" (a linked list) of a couple of
  // these descriptors.
  struct virtq_desc *desc;

  // a ring in which the driver writes descriptor numbers
  // that the driver would like the device to process.  it only
  // includes the head descriptor of each chain. the ring has
  // NUM elements.
  struct virtq_avail *avail;  //some thing different.

  // a ring in which the device writes descriptor numbers that
  // the device has finished processing (just the head of each chain).
  // there are NUM used ring entries.
  struct virtq_used *used;

  // our own book-keeping.
  char free[NUM];  // is a descriptor free?
  uint16 used_idx; // we've looked this far in used[2..NUM].

  // track info about in-flight operations,
  // for use when completion interrupt arrives.
  // indexed by first descriptor index of chain.
  struct {
    struct buf *buf;      // @s xv6 -> struct buf
    int finish;
    char status;
  } info[NUM];

  // 用于存储磁盘命令头信息。
  // 与描述符一一对应
  struct virtio_blk_req ops[NUM];
  
  struct spinlock vdisk_lock;
  
} disk;


/*
 * * * * * * * * * * * * * * * * 
 * 虚拟设备基本启动流程
 * * * * * * * * * * * * * * * * 
 * 1.重置设备，设置状态寄存器为0即可。
 * 2.设置状态寄存器Acknowladge状态位，OS识别到设备
 * 3.设置状态寄存器Driver状态位，OS知道如何驱动设备
 * 4.读取设备features，做设备状态协商。
 * 5.设置Features_OK状态位，驱动不再接收新的工作特性
 * 6.再次读取设备状态，确保已经设置Features_OK状态位。
 * 7.执行特定设备的设置，包括虚拟队列等(虚拟队列配置流程后方详细展开)
 * 8.设置DRIVER_OK 状态位，此时设备就活起来了，是的，非常优雅，不愧是标准。(原话：At this point the device is “live")
 * 
 * * * * * * * * * * * * * * * * * * 
 * 设备启动中虚拟队列基本配置流程
 * * * * * * * * * * * * * * * * * * 
 * 1.选择要使用的队列，将它的下标写入QueueSel，xv6使用默认的0号队列
 * 2.检查QueueReady寄存器判断队列是否就绪
 * 3.读取QueueNumMax寄存器，得到设备支持的最大队列大小
 * 4.给队列分配内存空间，确保物理上连续
 * 5.通过写入QueueNum通知设备驱动使用的队列大小
 * 6.写寄存器组：QueueDescLow/QueueDescHigh, QueueDriverLow/QueueDriverHigh and QueueDeviceLow/QueueDeviceHigh分别写入Descriptor Table Available Ring和Used Ring的64位地址。
 * 7.向QueueReady寄存器写1。准备完毕。
*/
void
devinit()
{
  uint32 status = 0;
  // 初始化锁
  initlock(&disk.vdisk_lock, "virtio_disk");
  /* 判断只读寄存器的值，确认设备正确 */
  if(*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
     *R(VIRTIO_MMIO_VERSION) != 1 ||
     *R(VIRTIO_MMIO_DEVICE_ID) != 2 ||
     *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551){
    panic("could not find virtio disk");
  }
  /* 1. 重置设备 */
  *R(VIRTIO_MMIO_STATUS) = status;

  /* 2.OS 识别到设备 */
  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  *R(VIRTIO_MMIO_STATUS) = status;

  /*3.OS知道如何驱动设备 */ 
  status |= VIRTIO_CONFIG_S_DRIVER;
  *R(VIRTIO_MMIO_STATUS) = status;

  /* 4.读取设备features，做设备状态协商。*/
  uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
  features &= ~(1 << VIRTIO_BLK_F_RO);
  features &= ~(1 << VIRTIO_BLK_F_SCSI);
  features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
  features &= ~(1 << VIRTIO_BLK_F_MQ);
  features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
  features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
  features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
  *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;

  // tell device that feature negotiation is complete.
  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // re-read status to ensure FEATURES_OK is set.
  status = *R(VIRTIO_MMIO_STATUS);
  if(!(status & VIRTIO_CONFIG_S_FEATURES_OK))
    panic("virtio disk FEATURES_OK unset");

  // initialize queue 0.
  *R(VIRTIO_MMIO_QUEUE_SEL) = 0;

  // ensure queue 0 is not in use.
  if(*R(VIRTIO_MMIO_QUEUE_READY))
    panic("virtio disk should not be ready");

  // check maximum queue size.
  uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
  if(max == 0)
    panic("virtio disk has no queue 0");
  if(max < NUM)
    panic("virtio disk max queue too short");

  // 分配物理地址连续空间
  disk.desc = kalloc();
  disk.avail = kalloc();
  disk.used = kalloc();
  if(!disk.desc || !disk.avail || !disk.used)
    panic("virtio disk kalloc");
  memset(disk.desc, 0, PGSIZE);
  memset(disk.avail, 0, PGSIZE);
  memset(disk.used, 0, PGSIZE);

  // set queue size.
  *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;

  // write physical addresses.
  *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)disk.desc;
  *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)disk.desc >> 32;
  *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)disk.avail;
  *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)disk.avail >> 32;
  *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)disk.used;
  *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)disk.used >> 32;

  // queue is ready.
  *R(VIRTIO_MMIO_QUEUE_READY) = 0x1;

  // 记录descriptor的可用情况，为1，可用，0不可用
  for(int i = 0; i < NUM; i++)
    disk.free[i] = 1;

  // tell device we're completely ready.
  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // plic.c and trap.c arrange for interrupts from VIRTIO0_IRQ.
}

// find a free descriptor, mark it non-free, return its index.
static int
alloc_desc()
{
  for(int i = 0; i < NUM; i++){
    if(disk.free[i]){
      disk.free[i] = 0;
      return i;
    }
  }
  return -1;
}

// mark a descriptor as free.
static void
free_desc(int i)
{
  if(i >= NUM)
    panic("free_desc 1");
  if(disk.free[i])
    panic("free_desc 2");
  disk.desc[i].addr = 0;
  disk.desc[i].len = 0;
  disk.desc[i].flags = 0;
  disk.desc[i].next = 0;
  disk.free[i] = 1;
  wakeup(&disk.free[0]);
}

/*释放一个描述符链*/
static void
free_chain(int i)
{
  while(1){
    int flag = disk.desc[i].flags;
    int nxt = disk.desc[i].next;
    free_desc(i);
    if(flag & VRING_DESC_F_NEXT)
      i = nxt;
    else
      break;
  }
}

// allocate three descriptors (they need not be contiguous).
// disk transfers always use three descriptors.
static int
alloc3_desc(int *idx)
{
  for(int i = 0; i < 3; i++){
    idx[i] = alloc_desc();
    if(idx[i] < 0){
      for(int j = 0; j < i; j++)
        free_desc(idx[j]);
      return -1;
    }
  }
  return 0;
}

static void
virtioRw(struct buf *buf, uint64 sector, int write)   // sector 磁盘的扇区 buf 存放数据的缓冲区 write 读/写磁盘
{
  acquire(&disk.vdisk_lock);

  int idx[3];

  // disk.desc描述一个物理内存块，用一个描述符数组(idx)集中管理所有的描述符
  // 如何分配成功就开始读/写
  // 否则就说明有进程占用了设备的输入输出资源
  // 就会睡眠,等待
  while (1) {
    if (alloc3_desc(idx) == 0)
      break;
    sleep(&disk.free[0], &disk.vdisk_lock);
  }

  struct virtio_blk_req *req = &disk.ops[idx[0]];

  if (write)
    req->type = VIRTIO_BLK_T_OUT;
  else 
    req->type = VIRTIO_BLK_T_IN;

  req->reserved = 0;

  req->sector = sector;

  disk.desc[idx[0]].addr = (uint64)req;
  disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
  disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
  disk.desc[idx[0]].next = idx[1];

  disk.desc[idx[1]].addr = (uint64)buf;
  disk.desc[idx[1]].len = BSIZE;
  if (write)
    disk.desc[idx[1]].flags = 0;
  else
    disk.desc[idx[1]].flags = VRING_DESC_F_WRITE;

  disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
  disk.desc[idx[1]].next = idx[2];

  disk.info[idx[0]].status = 0xff;

  disk.desc[idx[2]].addr = (uint64)&disk.info[idx[0]].status;
  disk.desc[idx[2]].len = 1;
  disk.desc[idx[2]].flags = VRING_DESC_F_WRITE;
  disk.desc[idx[2]].next = 0;

  // record struct buf for virtio_disk_intr().
  disk.info[idx[0]].finish = 0;
  disk.info[idx[0]].buf = buf;

  // tell the device the first index in our chain of descriptors.
  disk.avail->ring[disk.avail->idx % NUM] = idx[0];

  __sync_synchronize();

  // tell the device another avail ring entry is available.
  disk.avail->idx += 1; // not % NUM ...

  __sync_synchronize();

  // qemu mmio control register mapped 0x10001000 
  // VIRTIO_MMIO_QUEUE_NOTIFY -> write-only
  *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

  while (disk.info[idx[0]].finish == 0) {
    // 等待 virtiointr() 发出请求已完成的信号
    sleep(buf, &disk.vdisk_lock);
  }

  disk.info[idx[0]].buf = 0;
  free_chain(idx[0]);
  release(&disk.vdisk_lock);
}

// trap.c 在处理设备发出的中断时需要调用
// 所以没有同 virtioRw 一样 使用 static
void
virtiointr()
{
  acquire(&disk.vdisk_lock);

  // the device won't raise another interrupt until we tell it
  // we've seen this interrupt, which the following line does.
  // this may race with the device writing new entries to
  // the "used" ring, in which case we may process the new
  // completion entries in this interrupt, and have nothing to do
  // in the next interrupt, which is harmless.
  *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;

  __sync_synchronize();

  // the device increments disk.used->idx when it
  // adds an entry to the used ring.

  while(disk.used_idx != disk.used->idx){
    __sync_synchronize();
    int id = disk.used->ring[disk.used_idx % NUM].id;

    if(disk.info[id].status != 0)
      panic("virtio_disk_intr status");

    disk.info[id].finish = 1;
    wakeup(disk.info[id].buf);

    disk.used_idx += 1;
  }

  release(&disk.vdisk_lock);
}

// 提供使用的接口函数
// disk read
void
Virtioread(struct buf* buf, int sectorno)
{
  virtioRw(buf, sectorno, 0);
}

// disk write
void
Virtiowrite(struct buf* buf, int sectorno)
{
  virtioRw(buf, sectorno, 1);
}
