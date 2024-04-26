/*
 * 定义了一些fat32文件系统相关的参数及数据结构
*/
#ifndef __FAT32_H__
#define __FAT32_H__

/* fst32 fs 相关的参数 */
/* 文件系统中的特殊值,文件属性,文件名的长度限制*/
#define ATTR_READ_ONLY         0x01
#define ATTR_HIDDEN            0x02
#define ATTR_SYSTEM            0x04
#define ATTR_VOLUME_ID         0x08
#define ATTR_DIRECTORY         0x10
#define ATTR_ARCHIVE           0x20
#define ATTR_LONG_NAME         0x0f
#define LAST_LONG_ENTRY        0x40
#define FAT32_EOC              0x0ffffff8
#define EMPTY_ENTRY            0xe5
#define END_OF_ENTRY           0x00
#define CHAR_LONG_NAME         13
#define CHAR_SHORT_NAME        11
#define FAT32_MAX_FILENAME     255
#define FAT32_MAX_PATH         260
#define ENTRY_CACHE_NUM        50


/* struct directory */
struct dirent {
    char filename[FAT32_MAX_FILENAME + 1];
    uchar  attribute;
    // uint8   create_time_tenth;
    // uint16  create_time;
    // uint16  create_date;
    // uint16  last_access_date;   
    uint32 first_clus;
    // uint16  last_write_time;
    // uint16  last_write_date;
    uint32 file_size;
    uint32 cur_clus;
    uint   clus_cnt;

    /* for os */
    uchar dev;
    uchar dirty;
    short valid;
    int ref;
    uint32 off;           /*游标  offset in the parent dir entry, for writing convenience */
    struct dirent* parent;
    struct dirent* next;
    struct dirent* prev;
    struct sleeplock lock;

};



#endif
