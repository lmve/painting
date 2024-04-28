struct buf;
struct context;
struct file;
struct inode;
struct pipe;
struct proc;
struct spinlock;
struct sleeplock;
struct stat;
struct superblock;


// proc.c
int         cpuid();
struct cpu* mycpu();
struct proc*myproc();
int         killed(struct proc *);
void        sleep(void *, struct spinlock *);
int         either_copy(int user_dst, uint64 dst, void *src, uint64 len);
void        cpuinit(uint64);
void        sched();
void        reparent(struct proc *);
void        inittasktable();
void        initfirsttask();
void        exit(int);
void        setkilled(struct proc *p);
void        yield(void);
void        scheduler();
pagetable_t proc_pagetable(struct proc *);
void        forkret(void);
void        wakeup(void *);

// string.c
void *      memset(void *addr, int c, uint size);
void*       memmove(void *dst, const void *src, uint n);
int         strncmp(const char *p, const char *q, uint n);
char*       strncpy(char *s, const char *t, int n);
char*       strchr(const char *s, char c);
int         strlen(const char *s);
void snstr(char *dst, uint16 const *src, int len);


// printf.c
void        printfinit();
void        panic(char *);
void        printf(char *, ...);
void        TODO();

// console.c
void        consoleinit();
void        consoleputc(int);
void        consoleintr(int);

// kalloc.c
void        kinit();
void        kfree(void *);
void        freerange(void *, void *);
void *      kalloc();

// vm.c
void        kvmmap(pagetable_t, uint64, uint64, uint64, int);
void        kvminit();
int         mappages(pagetable_t, uint64, uint64, uint64, int);
pte_t *     walk(pagetable_t, uint64, int);
void        kvminithart();
void        inithartvm();
int         uvmcopy(pagetable_t, pagetable_t, uint64);
int         copyout(pagetable_t, uint64, char *, uint64);
int         copyinstr(pagetable_t, char *, uint64, uint64);
int         copyin(pagetable_t, char *, uint64, uint64);
pagetable_t uvmcreate();
void        uvmfirst(pagetable_t, uchar*, uint);
void        freewalk(pagetable_t);
void        uvmfree(pagetable_t, uint64);
void        uvmunmap(pagetable_t, uint64, uint64, int);
uint64      uvmdealloc(pagetable_t, uint64, uint64);
uint64      walkaddr(pagetable_t, uint64);
int         copyout2(uint64 dstva, char *src, uint64 len);

// timer.c
void        timerinit();
void        setTimeout();
void        clockintr();

// bio.c
void        binit();
struct buf* bread(uint dev, uint sectorno);
void        bwrite(struct buf *);
void        brelse(struct buf *);


// spinlock.c
void      initlock(struct spinlock *, char *);
void      acquire(struct spinlock *);
void      release(struct spinlock *);
void      push_off();
void      pop_off();
int       holding(struct spinlock *);

// sleeplock.c
void      initsleeplock(struct sleeplock *, char *name);
void      acquiresleeplock(struct sleeplock *);
int       holdingsleep(struct sleeplock *);
void      releasesleeplock(struct sleeplock *);

// trap.c
void      trapinithart();
void      usertrap();
void      usertrapret();
int       devintr();
void      kerneltrap(void);

// plic.c
void      plicinit();
void      plicinithart();
int       irq_claim(void);
void      plic_complete(int);

// virtio.c
void      devinit();
void      virtiointr();
void      Virtioread(struct buf* buf, int sectorno);
void      Virtiowrite(struct buf* buf, int sectorno);

// syscall.c
void      syscall(void);

// disk.c
void disk_init();
void disk_read(struct buf* b);
void disk_write(struct buf* b);
void disk_intr();

//fat32.c
int fat32_init();
struct dirent *dirlookup(struct dirent *dp, char *filename, uint *poff);
char*           formatname(char *name);
void            emake(struct dirent *dp, struct dirent *ep, uint off);
struct dirent*  ealloc(struct dirent *dp, char *name, int attr);
struct dirent*  edup(struct dirent *entry);
void            eupdate(struct dirent *entry);
void            etrunc(struct dirent *entry);
void            eremove(struct dirent *entry);
void            eput(struct dirent *entry);
void            estat(struct dirent *ep, struct stat *st);
void            elock(struct dirent *entry);
void            eunlock(struct dirent *entry);
int             enext(struct dirent *dp, struct dirent *ep, uint off, int *count);
struct dirent*  ename(char *path);
struct dirent*  enameparent(char *path, char *name);
int             eread(struct dirent *entry, int user_dst, uint64 dst, uint off, uint n);
int             ewrite(struct dirent *entry, int user_src, uint64 src, uint off, uint n);

// file.c
void            fileinit(void);