typedef unsigned long uint64;
typedef unsigned int  uint;
typedef unsigned short uint16;

typedef unsigned long size_t;
typedef unsigned char uchar;

typedef unsigned int  uint32;

#define NULL 0

/* 从指定的地址读取对应字节数据 */
#define readb(addr) (*(volatile uint8 *)(addr))
#define readw(addr) (*(volatile uint16 *)(addr))
#define readd(addr) (*(volatile uint32 *)(addr))
#define readq(addr) (*(volatile uint64 *)(addr))
/* 对指定的地址写入对应字节数据 */
#define writeb(v, addr)                      \
    {                                        \
        (*(volatile uint8 *)(addr)) = (v); \
    }
#define writew(v, addr)                       \
    {                                         \
        (*(volatile uint16 *)(addr)) = (v); \
    }
#define writed(v, addr)                       \
    {                                         \
        (*(volatile uint32 *)(addr)) = (v); \
    }
#define writeq(v, addr)                       \
    {                                         \
        (*(volatile uint64 *)(addr)) = (v); \
    }
/* 计算数组x的元素个数 */
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))