/*
 * 2.物理文件层
*/
/*
+ FAT32不支持日志系统，我们去掉了xv6文件系统的log层（log.c）；
+ FAT32没有inode，文件的元数据直接存放在目录项中，因此我们去掉了`struct inode`，替换为目录项`struct dirent`（directory entry）；
+ FAT32没有link，因此删除了相关的系统调用；
+ 重新实现xv6文件系统（fs.c）中的各个函数，将函数接口中的inode替换成了entry，函数命名上保留原函数的特征但也做了修改以示区分，如`ilock`变为`elock`、`writei`变为`ewrite`等等；
+ 关于buf层，由于FAT32的一个簇的大小较大，并且依不同的存储设备而变，因此我们目前以扇区为单位作缓存。*/
#include "types.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "types.h"
#include "param.h"
#include "riscv.h"
#include "proc.h"
#include "buf.h"
#include "fat32.h"
#include "defs.h"
#include "stat.h"

/*
 * * * * * * * * * * * * * * * * * * * * * * * * * 
 * 目录等相关数据的定义 
*/
typedef struct short_name_entry {
    char        name[CHAR_SHORT_NAME];
    uchar       attr;
    uchar       _nt_res;
    uchar       _crt_time_tenth;
    uint16      _crt_time;
    uint16      _crt_date;
    uint16      _lst_acce_date;
    uint16      fst_clus_hi;
    uint16      _lst_wrt_time;
    uint16      _lst_wrt_date;
    uint16      fst_clus_lo;
    uint32      file_size;
} __attribute__((packed, aligned(4))) short_name_entry_t;

typedef struct long_name_entry {
    uchar        order;
    uint16       name1[5];
    uchar        attr;     
    uchar        _type;
    uchar        checksum;  
    uint16       name2[6];
    uint16       _fst_clus_lo;
    uint16       name3[2];

} __attribute__((packed, aligned(4))) long_name_entry_t;

union dentry
{
    /* data */
    short_name_entry_t  shortname;
    long_name_entry_t   longname;
};

static struct {
    uint32 first_data_sec;
    uint32 data_sec_cnt;
    uint32 data_clus_cnt;
    uint32 byts_per_clus;

    struct {
        uint16 byts_per_sec;
        uchar sec_per_clus;
        uint16 rsvd_sec_cnt; /* 保留扇区数 */
        uchar fat_cnt;       /* FAT 区数量*/
        uint32 hidd_sec;     /* 隐藏扇区数 */
        uint32 tot_sec;      /* 总扇区数 */
        uint32 fat_sz;       /* FAT 区大小 */
        uint32 root_clus;    /* 根节点簇号 */
    }bpb;  /* 存储 BIOS 参数块 */

}fat;

static struct entry_cache{
    struct spinlock lock;
    struct dirent entries[ENTRY_CACHE_NUM];
}ecache;

static struct dirent root;

/*
 * * * * * * * * * * * * * * * * * * * * * * * * *
 * 初始化数据 
 * * * * * * * * * * * * * * * * * * * * * * * * *
 * 读取元数据
 * @ return 0    success
 *          -1   fail 
*/

int fat32_init(){
    /* for test */
    printf("[fat32_init] enter\n");

    struct buf* b = bread(0, 0); /* 设备号与扇区号 */
    /* 检查扇区数据中的特定标识 FAT32 */
    printf("read is ok!\n");
    if (strncmp((char const*)(b->data + 82),"FAT32",5))
        panic("[fat32_init] not fat32");
    /* 通过memmove()函数和直接赋值的方式，从扇区数据中解析出每项参数， */
    memmove(&fat.bpb.byts_per_sec,b->data + 11,2);                     /* 每扇区的字节数 */
    fat.bpb.sec_per_clus = b->data[13];                                /* 每簇的扇区数 */
    fat.bpb.rsvd_sec_cnt = *(uint16 *)(b->data + 14);                  /* 保留扇区数 */
    fat.bpb.fat_cnt = *(b->data + 16);                                 /* FAT 区数量 */
    fat.bpb.hidd_sec = *(uint32 *)(b->data + 28);                      /* 隐藏扇区数 */
    fat.bpb.tot_sec = *(uint32 *)(b->data + 32);                       /* 总扇区数 */
    fat.bpb.fat_sz = *(uint32 *)(b->data + 36);                        /* FAT 区大小 */
    fat.bpb.root_clus = *(uint32 *)(b->data + 44);                     /* 根目录起始簇号 */
    fat.first_data_sec = fat.bpb.rsvd_sec_cnt + fat.bpb.fat_cnt * fat.bpb.fat_sz;  /* 第一个数据区扇区号 */
    fat.data_sec_cnt = fat.bpb.tot_sec - fat.first_data_sec;           /* 数据区中扇区数量 */
    fat.data_clus_cnt = fat.data_sec_cnt / fat.bpb.sec_per_clus;       /* 数据区中簇的数量 */
    fat.byts_per_clus = fat.bpb.sec_per_clus * fat.bpb.byts_per_sec;   /* 每簇的字节数 */
    brelse(b);

    /* for test*/
    printf("[FAT32 init]byts_per_sec: %d\n", fat.bpb.byts_per_sec);
    printf("[FAT32 init]root_clus: %d\n", fat.bpb.root_clus);
    printf("[FAT32 init]sec_per_clus: %d\n", fat.bpb.sec_per_clus);
    printf("[FAT32 init]fat_cnt: %d\n", fat.bpb.fat_cnt);
    printf("[FAT32 init]fat_sz: %d\n", fat.bpb.fat_sz);
    printf("[FAT32 init]first_data_sec: %d\n", fat.first_data_sec);
    /* 检查 BSIZE */ 
    if (BSIZE != fat.bpb.byts_per_sec) 
        panic("byts_per_sec != BSIZE");
    initlock(&ecache.lock, "ecache");
    /* 初始化根目录 */
    memset(&root, 0, sizeof(root));
    initsleeplock(&root.lock, "entry");
    root.attribute = (ATTR_DIRECTORY | ATTR_SYSTEM);
    root.first_clus = root.cur_clus = fat.bpb.root_clus;
    root.valid = 1;
    root.prev = &root;
    root.next = &root;
    /* 初始化目录缓存 ecache */
    for(struct dirent *de = ecache.entries; de < ecache.entries + ENTRY_CACHE_NUM; de++) {
        de->dev = 0;
        de->valid = 0;
        de->ref = 0;
        de->dirty = 0;
        de->parent = 0;
        de->next = root.next;
        de->prev = &root;
        initsleeplock(&de->lock, "entry");
        root.next->prev = de;
        root.next = de;
    }
    return 0;

}

/*
 * * * * * * * * * * * * * * * * * * * * * * * * *
 * fat表相关参数计算函数
 * @param    cluster    cluster number begin from 2
*/
static inline uint32 first_sec_of_clus(uint32 cluster) {
    return ((cluster - 2) * fat.bpb.sec_per_clus) + fat.first_data_sec;
}

/*
 * * * * * * * * * * * * * * * * * * * * * * * * *
 * 获取 FAT 表中给定数据簇对应的扇区号
 * * * * * * * * * * * * * * * * * * * * * * * * *
 * 磁盘上以扇区为单位，而被文件系统所感知管理的是簇
 *@param    cluster    数据簇号
 *@param    fat_num    FAT表编号 bpb::fat_cnt
 *@return   sector number
*/
static inline uint32 fat_sec_of_clus(uint32 cluster, uchar fat_num) {
    return fat.bpb.rsvd_sec_cnt + (cluster << 2) / fat.bpb.byts_per_sec + fat.bpb.fat_sz * (fat_num - 1);
}

/*
 * * * * * * * * * * * * * * * * * * * * * * * * *
 * 获取 FAT 表中给定数据簇在fat表中的偏移
 * * * * * * * * * * * * * * * * * * * * * * * * *
 * 磁盘上以扇区为单位，而被文件系统所感知管理的是簇
 *@param    cluster    数据簇号
*/
static inline uint32 fat_offset_of_clus(uint32 cluster) {
    return (cluster << 2) % fat.bpb.byts_per_sec;
}
/**
 * * * * * * * * * * * * * * * * * * * * * * * * *
 * fat 表的读写 
 * * * * * * * * * * * * * * * * * * * * * * * * *
 * 读取与给定簇号对应的FAT表内容。
 * @param cluster 想要读取其在FAT表中内容的簇号
 * @return 返回簇号的下一个簇号。如果集群号大于等于FAT32的结束簇号（EOC），则返回该簇号本身；
 *         如果集群号超出FAT表中簇的计数加1，则返回0。
 */
static uint32 read_fat(uint32 cluster)
{
    if (cluster >= FAT32_EOC) {
        return cluster;
    }
    if (cluster > fat.data_clus_cnt + 1) {     // 簇号从2开始，而不是从0，因此此处比较的是是否超出有效簇号范围
        return 0;
    }
    uint32 fat_sec = fat_sec_of_clus(cluster, 1); // 计算指定簇对应的FAT扇区

    struct buf *b = bread(0, fat_sec); // 从磁盘读取相应的FAT扇区
    uint32 next_clus = *(uint32 *)(b->data + fat_offset_of_clus(cluster)); // 从读取的扇区中获取下一个簇的号码
    brelse(b); // 释放读取的扇区
    return next_clus;
}
/**
 * 写入与给定簇号对应的FAT区域内容。
 * @param cluster 要写入其内容的簇号，在FAT表中对应
 * @param content 应为FAT链尾标志的下一个簇号的内容
 * @return 成功返回0，如果簇号超出范围则返回-1。
 */
static int write_fat(uint32 cluster, uint32 content)
{
    // 检查簇号是否超出FAT表所管理的簇的范围
    if (cluster > fat.data_clus_cnt + 1) {
        return -1;
    }
    // 计算对应簇号的FAT扇区
    uint32 fat_sec = fat_sec_of_clus(cluster, 1);
    // 读取该扇区到缓冲区
    struct buf *b = bread(0, fat_sec);
    // 计算在缓冲区中的偏移量
    uint off = fat_offset_of_clus(cluster);
    // 更新FAT表项
    *(uint32 *)(b->data + off) = content;
    // 写回修改后的缓冲区
    bwrite(b);
    // 释放缓冲区
    brelse(b);
    return 0;
}
/*
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * 
 * 以簇为单位的操作
*/

 /* 参数：  cluster - 指定要零化的簇的编号 */
static void zero_clus(uint32 cluster)
{
    // 计算簇起始扇区
    uint32 sec = first_sec_of_clus(cluster);
    struct buf *b;
    // 遍历簇中的所有扇区，将其数据区域清零
    for (int i = 0; i < fat.bpb.sec_per_clus; i++) {
        b = bread(0, sec++); // 读取扇区
        memset(b->data, 0, BSIZE); // 清零扇区数据
        bwrite(b); // 写回扇区
        brelse(b); // 释放缓冲区
    }
}


 /* 分配一个空簇
 * 参数：  dev - 设备编号
 * 返回值：  分配到的簇的编号 */
static uint32 alloc_clus(uchar dev)
{
    // 从FAT中查找第一个空闲簇
    struct buf *b;
    uint32 sec = fat.bpb.rsvd_sec_cnt;
    uint32 const ent_per_sec = fat.bpb.byts_per_sec / sizeof(uint32);
    for (uint32 i = 0; i < fat.bpb.fat_sz; i++, sec++) {
        b = bread(dev, sec); // 读取FAT扇区
        for (uint32 j = 0; j < ent_per_sec; j++) {
            // 查找未使用的簇
            if (((uint32 *)(b->data))[j] == 0) {
                // 标记簇为已使用
                ((uint32 *)(b->data))[j] = FAT32_EOC + 7;
                bwrite(b); // 写回FAT
                brelse(b); // 释放缓冲区
                uint32 clus = i * ent_per_sec + j;
                zero_clus(clus); // 零化新分配的簇
                return clus; // 返回簇号
            }
        }
        brelse(b); // 释放缓冲区
    }
    // 如果没有找到空闲簇，则抛出异常
    panic("no clusters");
    return NULL;
}

/*
 * 释放指定的簇
 * 参数：
 *   cluster - 要释放的簇的编号
 */
static void free_clus(uint32 cluster)
{
    // 将簇标记为未使用
    write_fat(cluster, 0);
}

/*
 * 读写指定簇的数据
 * 参数：
 *   cluster - 指定的簇号
 *   write - 指明是写操作（1）还是读操作（0）
 *   user - 指明操作是否代表用户空间（1）还是内核空间（0）
 *   data - 数据的地址
 *   off - 在簇内的偏移量
 *   n - 要读写的数据量
 * 返回值：
 *   实际读写的数据量
 * 
 * 问题： read / write 检查
 */
static uint rw_clus(uint32 cluster, int write, int user, uint64 data, uint off, uint n)
{
    // 检查偏移量和数据量是否超出簇的范围
    if (off + n > fat.byts_per_clus)
        panic("offset out of range");
    uint tot, m;
    struct buf *bp;
    uint sec = first_sec_of_clus(cluster) + off / fat.bpb.byts_per_sec;
    off = off % fat.bpb.byts_per_sec;

    int bad = 0;
    // 循环处理所有要读写的数据
    for (tot = 0; tot < n; tot += m, off += m, data += m, sec++) {
        bp = bread(0, sec); // 读取对应扇区
        m = BSIZE - off % BSIZE; // 计算本次操作在扇区内的字节数
        if (n - tot < m) {
            m = n - tot; // 调整本次操作的字节数
        }
        if (write) {
            // 执行写操作，并检查是否出错
            if ((bad = either_copy( user, data,bp->data + (off % BSIZE), m)) != -1) {
                bwrite(bp); // 写回扇区
            }
        } else {
            // 执行读操作，并检查是否出错
            bad = either_copy(user, data, bp->data + (off % BSIZE), m);
        }
        brelse(bp); // 释放缓冲区
        if (bad == -1) {
            break; // 如果有错误，则终止操作
        }
    }
    return tot; // 返回实际读写的数据量
}
/*
 * 根据给定的偏移量重新定位目录项的当前簇号
 * @param entry 要修改其cur_clus字段的目录项
 * @param off 从相对文件开始处的偏移量
 * @param alloc 当遇到FAT链末端时，是否分配新簇
 * @return 从新cur_clus开始的偏移量
*/
static int reloc_clus(struct dirent *entry, uint off, int alloc)
{
    int clus_num = off / fat.byts_per_clus; // 根据偏移量计算簇号
    // 循环处理，直到找到对应的簇号
    while (clus_num > entry->clus_cnt) {
        int clus = read_fat(entry->cur_clus); // 读取当前簇的下一个簇号
        if (clus >= FAT32_EOC) { // 如果当前簇是结束簇
            if (alloc) { // 如果允许分配新簇
                clus = alloc_clus(entry->dev); // 分配一个新簇
                write_fat(entry->cur_clus, clus); // 将分配的新簇写入FAT
            } else {
                entry->cur_clus = entry->first_clus; // 重置当前簇号
                entry->clus_cnt = 0; // 重置簇计数
                return -1; // 返回错误
            }
        }
        entry->cur_clus = clus; // 更新当前簇号
        entry->clus_cnt++; // 簇计数加1
    }
    // 如果计算出的簇号小于当前记录的簇号，重新从第一个簇开始查找
    if (clus_num < entry->clus_cnt) {
        entry->cur_clus = entry->first_clus;
        entry->clus_cnt = 0;
        while (entry->clus_cnt < clus_num) { // 循环查找指定的簇号
            entry->cur_clus = read_fat(entry->cur_clus); // 读取下一个簇号
            if (entry->cur_clus >= FAT32_EOC) { // 如果遇到结束簇，报错
                panic("reloc_clus");
            }
            entry->clus_cnt++; // 簇计数加1
        }
    }
    return off % fat.byts_per_clus; // 返回在簇内的偏移量
}
/* * * * * * * * * * * * * * * * * * * * * * * * *
 *类似于 inode 读写 
*/
/*
 * 从指定位置读取数据到用户空间。
 * 
 * @param entry 指向目录项的指针。
 * @param user_dst 用户空间的地址。
 * @param dst 内核空间的地址。
 * @param off 文件内的偏移量。
 * @param n 要读取的数据量。
 * @return 实际读取的数据量，出错返回0。
 */
int eread(struct dirent *entry, int user_dst, uint64 dst, uint off, uint n)
{
    // 检查读取范围是否合法或文件是否为目录。
    if (off > entry->file_size || off + n < off || (entry->attribute & ATTR_DIRECTORY)) {
        return 0;
    }
    // 调整读取量，以不超过文件实际大小。
    if (off + n > entry->file_size) {
        n = entry->file_size - off;
    }

    uint tot, m;
    // 循环读取数据，直到达到请求的数量。
    for (tot = 0; entry->cur_clus < FAT32_EOC && tot < n; tot += m, off += m, dst += m) {
        reloc_clus(entry, off, 0);
        m = fat.byts_per_clus - off % fat.byts_per_clus;
        if (n - tot < m) {
            m = n - tot;
        }
        // 尝试读取一个簇的数据。
        if (rw_clus(entry->cur_clus, 0, user_dst, dst, off % fat.byts_per_clus, m) != m) {
            break;
        }
    }
    return tot;
}

/*
 * 将用户空间的数据写入指定位置。
 * 
 * @param entry 指向目录项的指针。
 * @param user_src 用户空间的地址。
 * @param src 内核空间的地址。
 * @param off 文件内的偏移量。
 * @param n 要写入的数据量。
 * @return 实际写入的数据量，出错返回-1。
 */
int ewrite(struct dirent *entry, int user_src, uint64 src, uint off, uint n)
{
    // 检查写入范围是否合法，文件是否只读或写入位置是否超出32位范围。
    if (off > entry->file_size || off + n < off || (uint64)off + n > 0xffffffff
        || (entry->attribute & ATTR_READ_ONLY)) {
        return -1;
    }
    // 如果文件大小为0，分配一个簇并标记为dirty。
    if (entry->first_clus == 0) {   
        entry->cur_clus = entry->first_clus = alloc_clus(entry->dev);
        entry->clus_cnt = 0;
        entry->dirty = 1;
    }
    uint tot, m;
    // 循环写入数据，直到达到请求的数量。
    for (tot = 0; tot < n; tot += m, off += m, src += m) {
        reloc_clus(entry, off, 1);
        m = fat.byts_per_clus - off % fat.byts_per_clus;
        if (n - tot < m) {
            m = n - tot;
        }
        // 尝试写入一个簇的数据。
        if (rw_clus(entry->cur_clus, 1, user_src, src, off % fat.byts_per_clus, m) != m) {
            break;
        }
    }
    // 如果写入的数据量大于0，并且写入位置超过当前文件大小，则更新文件大小并标记为dirty。
    if(n > 0) {
        if(off > entry->file_size) {
            entry->file_size = off;
            entry->dirty = 1;
        }
    }
    return tot;
}

/*
 * 获取指定名称的目录项。
 * 
 * @param parent 父目录的目录项指针。
 * @param name 要查找的文件或目录名称。
 * @return 找到的目录项指针，未找到或出错返回NULL。
 */
static struct dirent *eget(struct dirent *parent, char *name)
{
    struct dirent *ep;
    acquire(&ecache.lock);
    // 如果提供了文件名，则在缓存中查找是否已有该目录项。
    if (name) {
        for (ep = root.next; ep != &root; ep = ep->next) {          
            // 查找与给定名称匹配的目录项。
            if (ep->valid == 1 && ep->parent == parent
                && strncmp(ep->filename, name, FAT32_MAX_FILENAME) == 0) {
                if (ep->ref++ == 0) {
                    ep->parent->ref++;
                }
                release(&ecache.lock);
                return ep;
            }
        }
    }
    // 在缓存中查找一个未被引用的目录项，用于新的目录项缓存。
    for (ep = root.prev; ep != &root; ep = ep->prev) {              
        if (ep->ref == 0) {
            ep->ref = 1;
            ep->dev = parent->dev;
            ep->off = 0;
            ep->valid = 0;
            ep->dirty = 0;
            release(&ecache.lock);
            return ep;
        }
    }
    panic("eget: insufficient ecache");
    return 0;
}

/*
 * * * * * * * * * * * * * * * * * * * * * * * * * * * 
 * 与目录相关的操作 
*/
// trim ' ' in the head and tail, '.' in head, and test legality
char *formatname(char *name)
{
    static char illegal[] = { '\"', '*', '/', ':', '<', '>', '?', '\\', '|', 0 };
    char *p;
    while (*name == ' ' || *name == '.') { name++; }
    for (p = name; *p; p++) {
        char c = *p;
        if (c < 0x20 || strchr(illegal, c)) {
            return 0;
        }
    }
    while (p-- > name) {
        if (*p != ' ') {
            p[1] = '\0';
            break;
        }
    }
    return name;
}

static void generate_shortname(char *shortname, char *name)
{
    static char illegal[] = { '+', ',', ';', '=', '[', ']', 0 };   // these are legal in l-n-e but not s-n-e
    int i = 0;
    char c, *p = name;
    for (int j = strlen(name) - 1; j >= 0; j--) {
        if (name[j] == '.') {
            p = name + j;
            break;
        }
    }
    while (i < CHAR_SHORT_NAME && (c = *name++)) {
        if (i == 8 && p) {
            if (p + 1 < name) { break; }            // no '.'
            else {
                name = p + 1, p = 0;
                continue;
            }
        }
        if (c == ' ') { continue; }
        if (c == '.') {
            if (name > p) {                    // last '.'
                memset(shortname + i, ' ', 8 - i);
                i = 8, p = 0;
            }
            continue;
        }
        if (c >= 'a' && c <= 'z') {
            c += 'A' - 'a';
        } else {
            if (strchr(illegal, c) != NULL) {
                c = '_';
            }
        }
        shortname[i++] = c;
    }
    while (i < CHAR_SHORT_NAME) {
        shortname[i++] = ' ';
    }
}

uchar cal_checksum(uchar* shortname)
{
    uchar sum = 0;
    for (int i = CHAR_SHORT_NAME; i != 0; i--) {
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + *shortname++;
    }
    return sum;
}

/**
 * Generate an on disk format entry and write to the disk. Caller must hold dp->lock
 * @param   dp          the directory
 * @param   ep          entry to write on disk
 * @param   off         offset int the dp, should be calculated via dirlookup before calling this
 */
void emake(struct dirent *dp, struct dirent *ep, uint off)
{
    if (!(dp->attribute & ATTR_DIRECTORY))
        panic("emake: not dir");
    if (off % sizeof(union dentry))
        panic("emake: not aligned");
    
    union dentry de;
    memset(&de, 0, sizeof(de));
    if (off <= 32) {
        if (off == 0) {
            strncpy(de.shortname.name, ".          ", sizeof(de.shortname.name));
        } else {
            strncpy(de.shortname.name, "..         ", sizeof(de.shortname.name));
        }
        de.shortname.attr = ATTR_DIRECTORY;
        de.shortname.fst_clus_hi = (uint16)(ep->first_clus >> 16);        // first clus high 16 bits
        de.shortname.fst_clus_lo = (uint16)(ep->first_clus & 0xffff);       // low 16 bits
        de.shortname.file_size = 0;                                       // filesize is updated in eupdate()
        off = reloc_clus(dp, off, 1);
        rw_clus(dp->cur_clus, 1, 0, (uint64)&de, off, sizeof(de));
    } else {
        int entcnt = (strlen(ep->filename) + CHAR_LONG_NAME - 1) / CHAR_LONG_NAME;   // count of l-n-entries, rounds up
        char shortname[CHAR_SHORT_NAME + 1];
        memset(shortname, 0, sizeof(shortname));
        generate_shortname(shortname, ep->filename);
        de.longname.checksum = cal_checksum((uchar *)shortname);
        de.longname.attr = ATTR_LONG_NAME;
        for (int i = entcnt; i > 0; i--) {
            if ((de.longname.order = i) == entcnt) {
                de.longname.order |= LAST_LONG_ENTRY;
            }
            char *p = ep->filename + (i - 1) * CHAR_LONG_NAME;
            uchar *w = (uchar *)de.longname.name1;
            int end = 0;
            for (int j = 1; j <= CHAR_LONG_NAME; j++) {
                if (end) {
                    *w++ = 0xff;            // on k210, unaligned reading is illegal
                    *w++ = 0xff;
                } else { 
                    if ((*w++ = *p++) == 0) {
                        end = 1;
                    }
                    *w++ = 0;
                }
                switch (j) {
                    case 5:     w = (uchar *)de.longname.name2; break;
                    case 11:    w = (uchar *)de.longname.name3; break;
                }
            }
            uint off2 = reloc_clus(dp, off, 1);
            rw_clus(dp->cur_clus, 1, 0, (uint64)&de, off2, sizeof(de));
            off += sizeof(de);
        }
        memset(&de, 0, sizeof(de));
        strncpy(de.shortname.name, shortname, sizeof(de.shortname.name));
        de.shortname.attr = ep->attribute;
        de.shortname.fst_clus_hi = (uint16)(ep->first_clus >> 16);      // first clus high 16 bits
        de.shortname.fst_clus_lo = (uint16)(ep->first_clus & 0xffff);     // low 16 bits
        de.shortname.file_size = ep->file_size;                         // filesize is updated in eupdate()
        off = reloc_clus(dp, off, 1);
        rw_clus(dp->cur_clus, 1, 0, (uint64)&de, off, sizeof(de));
    }
}

/**
 * Allocate an entry on disk. Caller must hold dp->lock.
 */
struct dirent *ealloc(struct dirent *dp, char *name, int attr)
{
    if (!(dp->attribute & ATTR_DIRECTORY)) {
        panic("ealloc not dir");
    }
    if (dp->valid != 1 || !(name = formatname(name))) {        // detect illegal character
        return NULL;
    }
    struct dirent *ep;
    uint off = 0;
    if ((ep = dirlookup(dp, name, &off)) != 0) {      // entry exists
        return ep;
    }
    ep = eget(dp, name);
    elock(ep);
    ep->attribute = attr;
    ep->file_size = 0;
    ep->first_clus = 0;
    ep->parent = edup(dp);
    ep->off = off;
    ep->clus_cnt = 0;
    ep->cur_clus = 0;
    ep->dirty = 0;
    strncpy(ep->filename, name, FAT32_MAX_FILENAME);
    ep->filename[FAT32_MAX_FILENAME] = '\0';
    if (attr == ATTR_DIRECTORY) {    // generate "." and ".." for ep
        ep->attribute |= ATTR_DIRECTORY;
        ep->cur_clus = ep->first_clus = alloc_clus(dp->dev);
        emake(ep, ep, 0);
        emake(ep, dp, 32);
    } else {
        ep->attribute |= ATTR_ARCHIVE;
    }
    emake(dp, ep, off);
    ep->valid = 1;
    eunlock(ep);
    return ep;
}

struct dirent *edup(struct dirent *entry)
{
    if (entry != 0) {
        acquire(&ecache.lock);
        entry->ref++;
        release(&ecache.lock);
    }
    return entry;
}

// Only update filesize and first cluster in this case.
// caller must hold entry->parent->lock
void eupdate(struct dirent *entry)
{
    if (!entry->dirty || entry->valid != 1) { return; }
    uint entcnt = 0;
    uint32 off = reloc_clus(entry->parent, entry->off, 0);
    rw_clus(entry->parent->cur_clus, 0, 0, (uint64) &entcnt, off, 1);
    entcnt &= ~LAST_LONG_ENTRY;
    off = reloc_clus(entry->parent, entry->off + (entcnt << 5), 0);
    union dentry de;
    rw_clus(entry->parent->cur_clus, 0, 0, (uint64)&de, off, sizeof(de));
    de.shortname.fst_clus_hi = (uint16)(entry->first_clus >> 16);
    de.shortname.fst_clus_lo = (uint16)(entry->first_clus & 0xffff);
    de.shortname.file_size = entry->file_size;
    rw_clus(entry->parent->cur_clus, 1, 0, (uint64)&de, off, sizeof(de));
    entry->dirty = 0;
}

// caller must hold entry->lock
// caller must hold entry->parent->lock
// remove the entry in its parent directory
void eremove(struct dirent *entry)
{
    if (entry->valid != 1) { return; }
    uint entcnt = 0;
    uint32 off = entry->off;
    uint32 off2 = reloc_clus(entry->parent, off, 0);
    rw_clus(entry->parent->cur_clus, 0, 0, (uint64) &entcnt, off2, 1);
    entcnt &= ~LAST_LONG_ENTRY;
    uchar flag = EMPTY_ENTRY;
    for (int i = 0; i <= entcnt; i++) {
        rw_clus(entry->parent->cur_clus, 1, 0, (uint64) &flag, off2, 1);
        off += 32;
        off2 = reloc_clus(entry->parent, off, 0);
    }
    entry->valid = -1;
}

// truncate a file
// caller must hold entry->lock
void etrunc(struct dirent *entry)
{
    for (uint32 clus = entry->first_clus; clus >= 2 && clus < FAT32_EOC; ) {
        uint32 next = read_fat(clus);
        free_clus(clus);
        clus = next;
    }
    entry->file_size = 0;
    entry->first_clus = 0;
    entry->dirty = 1;
}

void elock(struct dirent *entry)
{
    if (entry == 0 || entry->ref < 1)
        panic("elock");
    acquiresleeplock(&entry->lock);
}

void eunlock(struct dirent *entry)
{
    if (entry == 0 || !holdingsleep(&entry->lock) || entry->ref < 1)
        panic("eunlock");
    releasesleeplock(&entry->lock);
}

void eput(struct dirent *entry)
{
    acquire(&ecache.lock);
    if (entry != &root && entry->valid != 0 && entry->ref == 1) {
        // ref == 1 means no other process can have entry locked,
        // so this acquiresleep() won't block (or deadlock).
        acquiresleeplock(&entry->lock);
        entry->next->prev = entry->prev;
        entry->prev->next = entry->next;
        entry->next = root.next;
        entry->prev = &root;
        root.next->prev = entry;
        root.next = entry;
        release(&ecache.lock);
        if (entry->valid == -1) {       // this means some one has called eremove()
            etrunc(entry);
        } else {
            elock(entry->parent);
            eupdate(entry);
            eunlock(entry->parent);
        }
        releasesleeplock(&entry->lock);

        // Once entry->ref decreases down to 0, we can't guarantee the entry->parent field remains unchanged.
        // Because eget() may take the entry away and write it.
        struct dirent *eparent = entry->parent;
        acquire(&ecache.lock);
        entry->ref--;
        release(&ecache.lock);
        if (entry->ref == 0) {
            eput(eparent);
        }
        return;
    }
    entry->ref--;
    release(&ecache.lock);
}

void estat(struct dirent *de, struct stat *st)
{
    strncpy(st->name, de->filename, STAT_MAX_NAME);
    st->type = (de->attribute & ATTR_DIRECTORY) ? T_DIR : T_FILE;
    st->dev = de->dev;
    st->size = de->file_size;
}

/**
 * 用于从目录项中提取文件名。该函数接受三个参数：
 * @param   buffer      指向存储提取出的文件名数组的指针。
 * @param   raw_entry   指向表示扇区缓冲区中目录项的union dentry结构的指针。
 * @param   islong      一个标志，表示是否将条目解释为长文件名（非零值）或短文件名（零值）。当前实现中并未使用此参数。
 */
static void read_entry_name(char *buffer, union dentry *d)
{
    if (d->longname.attr == ATTR_LONG_NAME) {                       // long entry branch
        uint16 temp[NELEM(d->longname.name1)];
        memmove(temp, d->longname.name1, sizeof(temp));
        snstr(buffer, temp, NELEM(d->longname.name1));
        buffer += NELEM(d->longname.name1);
        snstr(buffer, d->longname.name2, NELEM(d->longname.name2));
        buffer += NELEM(d->longname.name2);
        snstr(buffer, d->longname.name3, NELEM(d->longname.name3));
    } else {
        // assert: only "." and ".." will enter this branch
        memset(buffer, 0, CHAR_SHORT_NAME + 2); // plus '.' and '\0'
        int i;
        for (i = 0; d->shortname.name[i] != ' ' && i < 8; i++) {
            buffer[i] = d->shortname.name[i];
        }
        if (d->shortname.name[8] != ' ') {
            buffer[i++] = '.';
        }
        for (int j = 8; j < CHAR_SHORT_NAME; j++, i++) {
            if (d->shortname.name[j] == ' ') { break; }
            buffer[i] = d->shortname.name[j];
        }
    }
}

/**
 * Read entry_info from directory entry.
 * @param   entry       pointer to the structure that stores the entry info
 * @param   raw_entry   pointer to the entry in a sector buffer
 */
static void read_entry_info(struct dirent *entry, union dentry *d)
{
    entry->attribute = d->shortname.attr;
    entry->first_clus = ((uint32)d->shortname.fst_clus_hi << 16) | d->shortname.fst_clus_lo;
    entry->file_size = d->shortname.file_size;
    entry->cur_clus = entry->first_clus;
    entry->clus_cnt = 0;
}

/**
 * Read a directory from off, parse the next entry(ies) associated with one file, or find empty entry slots.
 * Caller must hold dp->lock.
 * @param   dp      the directory
 * @param   ep      the struct to be written with info
 * @param   off     offset off the directory
 * @param   count   to write the count of entries
 * @return  -1      meet the end of dir
 *          0       find empty slots
 *          1       find a file with all its entries
 */
int enext(struct dirent *dp, struct dirent *ep, uint off, int *count)
{
    if (!(dp->attribute & ATTR_DIRECTORY))
        panic("enext not dir");
    if (ep->valid)
        panic("enext ep valid");
    if (off % 32)
        panic("enext not align");
    if (dp->valid != 1) { return -1; }

    union dentry de;
    int cnt = 0;
    memset(ep->filename, 0, FAT32_MAX_FILENAME + 1);
    for (int off2; (off2 = reloc_clus(dp, off, 0)) != -1; off += 32) {
        if (rw_clus(dp->cur_clus, 0, 0, (uint64)&de, off2, 32) != 32 || de.longname.order == END_OF_ENTRY) {
            return -1;
        }
        if (de.longname.order == EMPTY_ENTRY) {
            cnt++;
            continue;
        } else if (cnt) {
            *count = cnt;
            return 0;
        }
        if (de.longname.attr == ATTR_LONG_NAME) {
            int lcnt = de.longname.order & ~LAST_LONG_ENTRY;
            if (de.longname.order & LAST_LONG_ENTRY) {
                *count = lcnt + 1;                              // plus the s-n-e;
                count = 0;
            }
            read_entry_name(ep->filename + (lcnt - 1) * CHAR_LONG_NAME, &de);
        } else {
            if (count) {
                *count = 1;
                read_entry_name(ep->filename, &de);
            }
            read_entry_info(ep, &de);
            return 1;
        }
    }
    return -1;
}

/**
 * Seacher for the entry in a directory and return a structure. Besides, record the offset of
 * some continuous empty slots that can fit the length of filename.
 * Caller must hold entry->lock.
 * @param   dp          entry of a directory file
 * @param   filename    target filename
 * @param   poff        offset of proper empty entry slots from the beginning of the dir
 */
struct dirent *dirlookup(struct dirent *dp, char *filename, uint *poff)
{
    if (!(dp->attribute & ATTR_DIRECTORY))
        panic("dirlookup not DIR");
    if (strncmp(filename, ".", FAT32_MAX_FILENAME) == 0) {
        return edup(dp);
    } else if (strncmp(filename, "..", FAT32_MAX_FILENAME) == 0) {
        if (dp == &root) {
            return edup(&root);
        }
        return edup(dp->parent);
    }
    if (dp->valid != 1) {
        return NULL;
    }
    struct dirent *ep = eget(dp, filename);
    if (ep->valid == 1) { return ep; }                               // ecache hits

    int len = strlen(filename);
    int entcnt = (len + CHAR_LONG_NAME - 1) / CHAR_LONG_NAME + 1;   // count of l-n-entries, rounds up. plus s-n-e
    int count = 0;
    int type;
    uint off = 0;
    reloc_clus(dp, 0, 0);
    while ((type = enext(dp, ep, off, &count) != -1)) {
        if (type == 0) {
            if (poff && count >= entcnt) {
                *poff = off;
                poff = 0;
            }
        } else if (strncmp(filename, ep->filename, FAT32_MAX_FILENAME) == 0) {
            ep->parent = edup(dp);
            ep->off = off;
            ep->valid = 1;
            return ep;
        }
        off += count << 5;
    }
    if (poff) {
        *poff = off;
    }
    eput(ep);
    return NULL;
}

static char *skipelem(char *path, char *name)
{
    while (*path == '/') {
        path++;
    }
    if (*path == 0) { return NULL; }
    char *s = path;
    while (*path != '/' && *path != 0) {
        path++;
    }
    int len = path - s;
    if (len > FAT32_MAX_FILENAME) {
        len = FAT32_MAX_FILENAME;
    }
    name[len] = 0;
    memmove(name, s, len);
    while (*path == '/') {
        path++;
    }
    return path;
}

// FAT32 version of namex in xv6's original file system.
static struct dirent *lookup_path(char *path, int parent, char *name)
{
    struct dirent *entry, *next;
    if (*path == '/') {
        entry = edup(&root);
    } else if (*path != '\0') {
        entry = edup(myproc()->cwd);
    } else {
        return NULL;
    }
    while ((path = skipelem(path, name)) != 0) {
        elock(entry);
        if (!(entry->attribute & ATTR_DIRECTORY)) {
            eunlock(entry);
            eput(entry);
            return NULL;
        }
        if (parent && *path == '\0') {
            eunlock(entry);
            return entry;
        }
        if ((next = dirlookup(entry, name, 0)) == 0) {
            eunlock(entry);
            eput(entry);
            return NULL;
        }
        eunlock(entry);
        eput(entry);
        entry = next;
    }
    if (parent) {
        eput(entry);
        return NULL;
    }
    return entry;
}

struct dirent *ename(char *path)
{
    char name[FAT32_MAX_FILENAME + 1];
    return lookup_path(path, 0, name);
}

struct dirent *enameparent(char *path, char *name)
{
    return lookup_path(path, 1, name);
}
