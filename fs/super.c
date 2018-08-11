/*
 *  linux/fs/super.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * super.c contains code to handle the super-block tables.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

#include <errno.h>
#include <sys/stat.h>

int sync_dev(int dev);
void wait_for_keypress(void);

/* set_bit uses setb, as gas doesn't recognize setc */
#define set_bit(bitnr,addr) ({ \
register int __res ; \
__asm__("bt %2,%3;setb %%al":"=a" (__res):"a" (0),"r" (bitnr),"m" (*(addr))); \
__res; })

struct super_block super_block[NR_SUPER];
/* this is initialized in init/main.c */
int ROOT_DEV = 0;

static void lock_super(struct super_block * sb)
{
	cli();
	while (sb->s_lock)
		sleep_on(&(sb->s_wait));
	sb->s_lock = 1;
	sti();
}

// unlock
static void free_super(struct super_block * sb)
{
	cli();
	sb->s_lock = 0;
	wake_up(&(sb->s_wait));
	sti();
}

static void wait_on_super(struct super_block * sb)
{
	cli();
	while (sb->s_lock)
		sleep_on(&(sb->s_wait));
	sti();
}

struct super_block * get_super(int dev)
{
	struct super_block * s;

	if (!dev)
		return NULL;

	s = 0+super_block;  // super_block[0]

	while (s < NR_SUPER+super_block) // s < super_block[NR_SUPER]
    {
		if (s->s_dev == dev) {
			wait_on_super(s);
			if (s->s_dev == dev)
				return s;
			s = 0+super_block;      // dirty 이기 때문에 처음부터 다시 비교
		} else
			s++;                    // increase
    }
	return NULL;
}

void put_super(int dev)
{
	struct super_block * sb;
	/* struct m_inode * inode;*/
	int i;

	if (dev == ROOT_DEV) {
		printk("root diskette changed: prepare for armageddon\n\r");
		return;
	}
	if (!(sb = get_super(dev)))
		return;
	if (sb->s_imount) {
		printk("Mounted disk changed - tssk, tssk\n\r");
		return;
	}
	lock_super(sb);
	sb->s_dev = 0;
	for(i=0;i<I_MAP_SLOTS;i++)
		brelse(sb->s_imap[i]);
	for(i=0;i<Z_MAP_SLOTS;i++)
		brelse(sb->s_zmap[i]);
	free_super(sb);
	return;
}

static struct super_block * read_super(int dev)
{
	struct super_block * s;
	struct buffer_head * bh;
	int i,block;

	if (!dev)
		return NULL;

	check_disk_change(dev);     // 디스크가 변경되었는지 확인

	if ((s = get_super(dev)))
		return s;

	for (s = 0+super_block ;; s++)      // super_block[0]
    {
		if (s >= NR_SUPER+super_block)  // super_block[NR_SUPER]
			return NULL;                // 빈 super_block 이 없으면 

		if (!s->s_dev)                  // 비어 있으면, 초기값이 0
			break;
	}

	s->s_dev = dev;
	s->s_isup = NULL;
	s->s_imount = NULL;
	s->s_time = 0;
	s->s_rd_only = 0;
	s->s_dirt = 0;

	lock_super(s);

	if (!(bh = bread(dev,1))) {     // 1 block read
		s->s_dev=0;
		free_super(s);
		return NULL;
	}
    
	*((struct d_super_block *) s) =
		*((struct d_super_block *) bh->b_data);
	brelse(bh);

    // magic number validate
	if (s->s_magic != SUPER_MAGIC) {
		s->s_dev = 0;
		free_super(s);
		return NULL;
	}

    // 초기화
	for (i=0;i<I_MAP_SLOTS;i++)
		s->s_imap[i] = NULL; //inode bit map
	for (i=0;i<Z_MAP_SLOTS;i++)
		s->s_zmap[i] = NULL; //data bit map

	block=2;
    // s->s_imap_blocks 의 경우   
    // *((struct d_super_block *) s) = *((struct d_super_block *) bh->b_data);
    // 에서 설정된 값
	for (i=0 ; i < s->s_imap_blocks ; i++)
		if ( (s->s_imap[i]=bread(dev,block)) )
			block++;
		else
			break;

	for (i=0 ; i < s->s_zmap_blocks ; i++)
		if ((s->s_zmap[i]=bread(dev,block)))
			block++;
		else
			break;

    // 읽은 개수와 메타 정보가 다르면 
	// 논리 블록 개수가 디바이스가 가져야 할 블록 개수와 동일한지 확인 
	if (block != 2+s->s_imap_blocks+s->s_zmap_blocks)
    {
		for(i=0;i<I_MAP_SLOTS;i++)
			brelse(s->s_imap[i]);
		for(i=0;i<Z_MAP_SLOTS;i++)
			brelse(s->s_zmap[i]);
		s->s_dev=0;
		free_super(s);
		return NULL;
	}

	s->s_imap[0]->b_data[0] |= 1;
	s->s_zmap[0]->b_data[0] |= 1;

    // unlock
	free_super(s);

	return s;
}

int sys_umount(char * dev_name)
{
	struct m_inode * inode;
	struct super_block * sb;
	int dev;

	if (!(inode=namei(dev_name)))
		return -ENOENT;
	dev = inode->i_zone[0];
	if (!S_ISBLK(inode->i_mode)) {
		iput(inode);
		return -ENOTBLK;
	}
	iput(inode);
	if (dev==ROOT_DEV)
		return -EBUSY;
	if (!(sb=get_super(dev)) || !(sb->s_imount))
		return -ENOENT;
	if (!sb->s_imount->i_mount)
		printk("Mounted inode has i_mount=0\n");
	for (inode=inode_table+0 ; inode<inode_table+NR_INODE ; inode++)
		if (inode->i_dev==dev && inode->i_count)
				return -EBUSY;
	sb->s_imount->i_mount=0;
	iput(sb->s_imount);
	sb->s_imount = NULL;
	iput(sb->s_isup);
	sb->s_isup = NULL;
	put_super(dev);
	sync_dev(dev);
	return 0;
}

int sys_mount(char * dev_name, char * dir_name, int rw_flag)
{
	struct m_inode * dev_i, * dir_i;
	struct super_block * sb;
	int dev;

	//dev_name = devfile
	if (!(dev_i=namei(dev_name)))
		return -ENOENT;
	dev = dev_i->i_zone[0];
	if (!S_ISBLK(dev_i->i_mode)) {
		iput(dev_i);
		return -EPERM;
	}
	iput(dev_i);

	//dir_name = target dir 
	if (!(dir_i=namei(dir_name)))
		return -ENOENT;

	if (dir_i->i_count != 1 || dir_i->i_num == ROOT_INO) 
	{
		iput(dir_i);
		return -EBUSY;
	}

	// #define S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR)
	if (!S_ISDIR(dir_i->i_mode)) {
		iput(dir_i);
		return -EPERM;
	}

	if (!(sb=read_super(dev))) 
    {
		iput(dir_i);
		return -EBUSY;
	}

	//이미 마운트가 되어 있는 경우 
	if (sb->s_imount) {
		iput(dir_i);
		return -EBUSY;
	}

	if (dir_i->i_mount) {
		iput(dir_i);
		return -EPERM;
	}

	sb->s_imount=dir_i;
	dir_i->i_mount=1;   // 파일 시스템이 마운트되었다는 것을 표시한다.
	dir_i->i_dirt=1;	// 정보가 변경 된 것을 표시한다.
						/* NOTE! we don't iput(dir_i) */
	return 0;			/* we do that in umount */
}

void mount_root(void)
{
	int i,free;
	struct super_block * p;
	struct m_inode * mi;

	if (32 != sizeof (struct d_inode))
		panic("bad i-node size");

    // 파일 테이블 초기화
	for(i=0;i<NR_FILE;i++)
		file_table[i].f_count=0;
	if (MAJOR(ROOT_DEV) == 2) {
		printk("Insert root floppy and press ENTER");
		wait_for_keypress();
	}

    // 구조체 초기화 
	for(p = &super_block[0] ; p < &super_block[NR_SUPER] ; p++) {
		p->s_dev = 0;
		p->s_lock = 0;
		p->s_wait = NULL;
	}

    // ROOT_DEV 0x0101
	if (!(p=read_super(ROOT_DEV)))
		panic("Unable to mount root");

	if (!(mi=iget(ROOT_DEV,ROOT_INO)))
		panic("Unable to read root i-node");

	mi->i_count += 3 ;	/* NOTE! it is logically used 4 times, not 1 */

	p->s_isup = p->s_imount = mi; // ??

	current->pwd = mi;
	current->root = mi;
	free=0;
	i=p->s_nzones;

	while (-- i >= 0)
    {
///* set_bit uses setb, as gas doesn't recognize setc */
//#define set_bit(bitnr,addr) ({ \
//register int __res ; \
//__asm__("bt %2,%3;                // bt i&8191, p->s_zmap[i>>13]->b_data
//         setb %%al":"=a" (__res)
//                   :"a" (0),
//                    "r" (bitnr),
//                    "m" (*(addr))); \
//__res; })
        // 8191 : 2^13 - 1
		if (!set_bit( i&8191,
                      p->s_zmap[i>>13]->b_data))
			free++;
    }

	printk("%d/%d free blocks\n\r",free,p->s_nzones);
	free=0;
	i=p->s_ninodes+1;

	while (-- i >= 0)
    {
		if (!set_bit(i&8191,p->s_imap[i>>13]->b_data))
			free++;
    }

	printk("%d/%d free inodes\n\r",free,p->s_ninodes);
}
