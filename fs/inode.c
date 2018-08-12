/*
 *  linux/fs/inode.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <string.h> 
#include <sys/stat.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/system.h>

struct m_inode inode_table[NR_INODE]={{0,},};

static void read_inode(struct m_inode * inode);
static void write_inode(struct m_inode * inode);

static inline void wait_on_inode(struct m_inode * inode)
{
	cli();
	while (inode->i_lock)
		sleep_on(&inode->i_wait);
	sti();
}

static inline void lock_inode(struct m_inode * inode)
{
	cli();
	while (inode->i_lock)
		sleep_on(&inode->i_wait);
	inode->i_lock=1;
	sti();
}

static inline void unlock_inode(struct m_inode * inode)
{
	inode->i_lock=0;
	wake_up(&inode->i_wait);
}

void invalidate_inodes(int dev)
{
	int i;
	struct m_inode * inode;

	inode = 0+inode_table;
	for(i=0 ; i<NR_INODE ; i++,inode++) {
		wait_on_inode(inode);
		if (inode->i_dev == dev) {
			if (inode->i_count)
				printk("inode in use on removed disk\n\r");
			inode->i_dev = inode->i_dirt = 0;
		}
	}
}

void sync_inodes(void)
{
	int i;
	struct m_inode * inode;

	inode = 0+inode_table;
	for( i = 0 ; i < NR_INODE ; i++, inode++) 
    {
		wait_on_inode(inode);
		if ( inode->i_dirt && !inode->i_pipe )
			write_inode(inode);
	}
}

static int _bmap(struct m_inode * inode,int block,int create)
{
	struct buffer_head * bh;
	int i;

	if ( block < 0 )
		panic("_bmap: block<0");

	if ( block >= (7 + 512 + 512*512) )
		panic("_bmap: block>big");

    // 7 블록 이하 일때 
	if ( block < 7 ) 
    {
        // 생성해야 하고
        // 해당 블록 위치에 izone 에 블록이 할당되어 있지 않으면
		if ( create && !inode->i_zone[block] )
        {
			if ((inode->i_zone[block]=new_block(inode->i_dev))) 
            {
				inode->i_ctime=CURRENT_TIME;
				inode->i_dirt=1;
			}
        }
		return inode->i_zone[block];
	}

    // 7 블록 이상 이고 7 + 512 이하면 
    // 1단계 간접 블록이면
	block -= 7;

	if ( block < 512 ) 
    {
		if ( create && !inode->i_zone[7] )
        {
			if ( (inode->i_zone[7] = new_block( inode->i_dev )) ) 
            {
				inode->i_dirt=1;
				inode->i_ctime=CURRENT_TIME;
			}
        }

		if (!inode->i_zone[7])
			return 0;

		if (!(bh = bread(inode->i_dev,inode->i_zone[7])))
			return 0;

		i = ((unsigned short *) (bh->b_data))[block];
		if ( create && !i )
        {
			if ((i=new_block(inode->i_dev))) 
            {
				((unsigned short *) (bh->b_data))[block]=i;
				bh->b_dirt=1;
			}
        }

		brelse(bh);

		return i;
	}

    // 7 + 512 이상이고 7 + 512 + 512 * 512 이하면
    // 2단계 간접블록 
	block -= 512;
	if (create && !inode->i_zone[8])
    {
		if ((inode->i_zone[8]=new_block(inode->i_dev))) 
        {
			inode->i_dirt=1;
			inode->i_ctime=CURRENT_TIME;
		}
    }

	if (!inode->i_zone[8])
		return 0;

	if (!(bh=bread(inode->i_dev,inode->i_zone[8])))
		return 0;

	i = ((unsigned short *)bh->b_data)[block>>9];   // block/512
	if (create && !i)
    {
		if ((i=new_block(inode->i_dev))) 
        {
			((unsigned short *) (bh->b_data))[block>>9] = i;
			bh->b_dirt=1;
		}
    }

	brelse(bh);

	if (!i)
		return 0;

	if (!(bh=bread(inode->i_dev,i)))
		return 0;

	i = ((unsigned short *)bh->b_data)[block&511];      // 2단계에서 블록넘버를 찾는다.
	if (create && !i)
    {
		if ((i=new_block(inode->i_dev))) 
        {
			((unsigned short *) (bh->b_data))[block&511]=i;
			bh->b_dirt=1;
		}
    }

	brelse(bh);

	return i;
}

int bmap(struct m_inode * inode,int block)
{
	return _bmap(inode,block,0);
}

int create_block(struct m_inode * inode, int block)
{
	return _bmap(inode,block,1);
}
		
void iput(struct m_inode * inode)
{
	if (!inode)
		return;
	wait_on_inode(inode);
	if (!inode->i_count)
		panic("iput: trying to free free inode");

	if (inode->i_pipe) {
		wake_up(&inode->i_wait);
		if (--inode->i_count)
			return;
		free_page(inode->i_size);
		inode->i_count=0;
		inode->i_dirt=0;
		inode->i_pipe=0;
		return;
	}

	if (!inode->i_dev)  // empty에 대한 처리`
    {
		inode->i_count--;
		return;
	}
	if (S_ISBLK(inode->i_mode)) {
		sync_dev(inode->i_zone[0]);
		wait_on_inode(inode);
	}
repeat:
	if (inode->i_count>1) {
		inode->i_count--;
		return;
	}
	if (!inode->i_nlinks) {
		truncate(inode);
		free_inode(inode);
		return;
	}
	if (inode->i_dirt) {
		write_inode(inode);	/* we can sleep - so do again */
		wait_on_inode(inode);
		goto repeat;
	}
	inode->i_count--;
	return;
}

struct m_inode * get_empty_inode(void)
{
	struct m_inode * inode;
	static struct m_inode * last_inode = inode_table;
	int i;

	do {
		inode = NULL;

        // NR_INODE : 32
		for (i = NR_INODE; i ; i--) 
        {
			if (++last_inode >= inode_table + NR_INODE) // last_inode >= inode_table[NR_INODE]
				last_inode = inode_table;      // inode_table에서 벗어나면 다시 최초위치로 

            // last_inode->i_count 가 0 이면 
            // last_inode 가 할당되어 있지 않으면
			if (!last_inode->i_count) { 
				inode = last_inode;
                // 깨끗하면 
				if (!inode->i_dirt && !inode->i_lock)
					break;
			}
		}

        // 위 for 문에서 inode 을 못찾으면 panic
		if (!inode) {
			for (i=0 ; i<NR_INODE ; i++)
				printk("%04x: %6d\t",inode_table[i].i_dev,
					inode_table[i].i_num);
			panic("No free inodes in mem");
		}

        // wait unlock 
		wait_on_inode(inode);

        // dirty 일 경우
		while (inode->i_dirt) {
			write_inode(inode);
			wait_on_inode(inode);
		}
	} while (inode->i_count);   // 참조 카운트가 존재하면 do 처음으로

	memset(inode,0,sizeof(*inode));
	inode->i_count = 1;
	return inode;
}

struct m_inode * get_pipe_inode(void)
{
	struct m_inode * inode;

	if (!(inode = get_empty_inode()))
		return NULL;
	if (!(inode->i_size=get_free_page())) {
		inode->i_count = 0;
		return NULL;
	}
	inode->i_count = 2;	/* sum of readers/writers */
	PIPE_HEAD(*inode) = PIPE_TAIL(*inode) = 0;
	inode->i_pipe = 1;
	return inode;
}

struct m_inode * iget(int dev,int nr)
{
	struct m_inode * inode, * empty;

	if (!dev)
		panic("iget with dev==0");

	empty = get_empty_inode();
	inode = inode_table;
    
	while (inode < NR_INODE+inode_table) 
    {
        // 같은 inode 찾기
		if (inode->i_dev != dev || inode->i_num != nr) {
			inode++;
			continue;
		}
        // lock 이 풀릴 때 까지 대기
		wait_on_inode(inode);
        // 같은 inode 비교
		if (inode->i_dev != dev || inode->i_num != nr) {
			inode = inode_table; // 다를 경우 처음부터
			continue;
		}

		inode->i_count++;       // 참조 카운트 증가
		if (inode->i_mount) 
		{
			int i;

			for (i = 0 ; i<NR_SUPER ; i++)
				if (super_block[i].s_imount==inode)
					break;
			if (i >= NR_SUPER) 
			{
				printk("Mounted inode hasn't got sb\n");
				if (empty)
					iput(empty);
				return inode;
			}
			iput(inode);
			dev = super_block[i].s_dev;
			nr = ROOT_INO;
			inode = inode_table;
			continue;
		}

		if (empty)          // empty 검사가 굳이 필요한가???
			iput(empty);    // empty 가 필요 없으므로 다시 반납 

		return inode;
	}

	if (!empty)
		return (NULL);

	inode=empty;
	inode->i_dev = dev;
	inode->i_num = nr;
    
	read_inode(inode);

	return inode;
}

static void read_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;
	int block;

	lock_inode(inode);
	if (!(sb=get_super(inode->i_dev)))
		panic("trying to read inode without dev");

    // inode 가 저장된 위치를 찾는다.
	block = 2 +                     // super block
            sb->s_imap_blocks +     // i-node bitmap 
            sb->s_zmap_blocks +     // 논리block bitmap
		    (inode->i_num-1) / INODES_PER_BLOCK;
    
    // 저장된 위치에서 inode 정보를 읽고
	if (!(bh=bread(inode->i_dev,block)))
		panic("unable to read i-node block");
    
    // disk 로 부터 읽은 inode 정보를 구조체에 설정
    // d_inode 는 m_inode 에 포함 된다.
	*(struct d_inode *)inode =
		((struct d_inode *)bh->b_data)
			[(inode->i_num-1)%INODES_PER_BLOCK];

	brelse(bh);
	unlock_inode(inode);
}

static void write_inode(struct m_inode * inode)
{
	struct super_block * sb;
	struct buffer_head * bh;
	int block;

	lock_inode(inode);

	if (!inode->i_dirt || !inode->i_dev) 
    {
		unlock_inode(inode);
		return;
	}

	if (!(sb=get_super(inode->i_dev)))
		panic("trying to write inode without device");

    // inode 비트맵의 논리 블록 넘버를 계산
	block = 2 + 
            sb->s_imap_blocks + 
            sb->s_zmap_blocks +
		    (inode->i_num-1)/INODES_PER_BLOCK;

    // 논리 블록을 읽어서
	if ( !(bh=bread(inode->i_dev,block)) )
		panic("unable to read i-node block");
    
    // inode 를 논리 블록의 저장할 위치에 저장
	((struct d_inode *)bh->b_data)[(inode->i_num-1)%INODES_PER_BLOCK] =
			*(struct d_inode *)inode;

	bh->b_dirt=1;
	inode->i_dirt=0;

	brelse(bh);

	unlock_inode(inode);
}
