/*
 *  linux/fs/file_dev.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <fcntl.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

int file_read(struct m_inode * inode, struct file * filp, char * buf, int count)
{
	int left,chars,nr;
	struct buffer_head * bh;

	if ( (left=count) <= 0 )
		return 0;

	while ( left ) 
    {
		if ( ( nr = bmap( inode, (filp->f_pos)/BLOCK_SIZE ) ) ) 
        {
			if ( !(bh=bread(inode->i_dev,nr)) )
				break;
		} 
        else
        {
            // TODO write 부분 보고 다시 체크
			bh = NULL;
        }
        
		nr = filp->f_pos % BLOCK_SIZE;
		chars = MIN( BLOCK_SIZE-nr , left );    // 해당 블록에서 복사할 크기를 결정 한다.
		filp->f_pos += chars;                   // file position을 변경
		left -= chars;                          // 해당 블록을 읽고 남은 크기를 계산

		if ( bh )   // 디스크에서 데이터를 가져 왔으면 
        {
			char * p = nr + bh->b_data;
			while (chars-->0)
				put_fs_byte(*(p++),buf++);

			brelse(bh);
		} 
        else        // 데이터를 못가져왔으면 0 으로 복사 
        {
			while (chars-->0)
				put_fs_byte(0,buf++);
		}
	}

	inode->i_atime = CURRENT_TIME;
    // 읽은 크기를 반환
	return (count-left)?(count-left):-ERROR;
}

int file_write(struct m_inode * inode, struct file * filp, char * buf, int count)
{
	off_t pos;
	int block,c;
	struct buffer_head * bh;
	char * p;
	int i=0;

/*
 * ok, append may not work when many processes are writing at the same time
 * but so what. That way leads to madness anyway.
 */
	if (filp->f_flags & O_APPEND)
		pos = inode->i_size;
	else
		pos = filp->f_pos;

	while ( i < count ) 
    {
		if ( !(block = create_block( inode, pos/BLOCK_SIZE ) ) )
			break;

		if ( !(bh=bread(inode->i_dev,block)) )
			break;

		c = pos % BLOCK_SIZE;       // block 내에 쓰기할 offset
		p = c + bh->b_data;         // 쓰여질 버퍼의 메모리 주소
		bh->b_dirt = 1;
		c = BLOCK_SIZE - c;         // offset 부터 쓰기 때문에 
                                    // block 내 남은 크기를 계산
		if ( c > count-i )          // 1 block 크기 보다 쓰여져야 하는 크기가 작으면
            c = count-i;             

		pos += c;
		if (pos > inode->i_size) 
        {
			inode->i_size = pos;
			inode->i_dirt = 1;
		}

		i += c;

		while ( c-- > 0 )
			*(p++) = get_fs_byte(buf++);

		brelse(bh);
	}

	inode->i_mtime = CURRENT_TIME;
	if ( !(filp->f_flags & O_APPEND) ) 
    {
		filp->f_pos = pos;
		inode->i_ctime = CURRENT_TIME;
	}

	return (i?i:-1);
}
