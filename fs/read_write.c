/*
 *  linux/fs/read_write.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <sys/stat.h>
#include <errno.h>
#include <sys/types.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/segment.h>

extern int rw_char(int rw,int dev, char * buf, int count, off_t * pos);
extern int read_pipe(struct m_inode * inode, char * buf, int count);
extern int write_pipe(struct m_inode * inode, char * buf, int count);
extern int block_read(int dev, off_t * pos, char * buf, int count);
extern int block_write(int dev, off_t * pos, char * buf, int count);
extern int file_read(struct m_inode * inode, struct file * filp,
		char * buf, int count);
extern int file_write(struct m_inode * inode, struct file * filp,
		char * buf, int count);

int sys_lseek(unsigned int fd,off_t offset, int origin)
{
	struct file * file;
	int tmp;

	if ( ( fd >= NR_OPEN ) || 
         !(file=current->filp[fd]) || 
         !(file->f_inode) || 
         !IS_SEEKABLE(MAJOR(file->f_inode->i_dev)) )
		return -EBADF;

	if (file->f_inode->i_pipe)
		return -ESPIPE;

	switch (origin) {
		case 0:
			if (offset<0) return -EINVAL;
			file->f_pos=offset;
			break;
		case 1:
			if (file->f_pos+offset<0) return -EINVAL;
			file->f_pos += offset;
			break;
		case 2:
			if ((tmp=file->f_inode->i_size+offset) < 0)
				return -EINVAL;
			file->f_pos = tmp;
			break;
		default:
			return -EINVAL;
	}
	return file->f_pos;
}

int sys_read(unsigned int fd,char * buf,int count)
{
	struct file * file;
	struct m_inode * inode;

	if ( ( fd >= NR_OPEN ) ||   // fd 값이 정상적인지
         ( count < 0 ) ||       // 읽을 바이트수가 정상적인지
         !(file = current->filp[fd])) // 열려 있는 fd 인지
		return -EINVAL;

	if ( !count )   // 읽을 크기가 0이면
		return 0;

	verify_area(buf,count);
    
	inode = file->f_inode;
    // read pipe 를 호출
	if (inode->i_pipe)
		return (file->f_mode&1)?read_pipe(inode,buf,count):-EIO;

	if (S_ISCHR(inode->i_mode))
		return rw_char(READ,inode->i_zone[0],buf,count,&file->f_pos);

	if (S_ISBLK(inode->i_mode))
		return block_read(inode->i_zone[0],&file->f_pos,buf,count);

    // 디렉토리거나 일반파일 이면
	if ( S_ISDIR(inode->i_mode) || S_ISREG(inode->i_mode) ) 
    {
        // 읽을 사이즈 보다 읽을 파일의 남은 사이즈가 더 클때 
        // 읽을 사이즈를 보정한다.
		if ( ( count + file->f_pos ) > inode->i_size )
			count = inode->i_size - file->f_pos;

		if ( count <= 0 )
			return 0;

		return file_read(inode,file,buf,count);
	}

	printk("(Read)inode->i_mode=%06o\n\r",inode->i_mode);

	return -EINVAL;
}

int sys_write(unsigned int fd,char * buf,int count)
{
	struct file * file;
	struct m_inode * inode;
	
	if (fd>=NR_OPEN || count <0 || !(file=current->filp[fd]))
		return -EINVAL;
	if (!count)
		return 0;
	inode=file->f_inode;
	if (inode->i_pipe)
		return (file->f_mode&2)?write_pipe(inode,buf,count):-EIO;
	if (S_ISCHR(inode->i_mode))
		return rw_char(WRITE,inode->i_zone[0],buf,count,&file->f_pos);
	if (S_ISBLK(inode->i_mode))
		return block_write(inode->i_zone[0],&file->f_pos,buf,count);
	if (S_ISREG(inode->i_mode))
		return file_write(inode,file,buf,count);
	printk("(Write)inode->i_mode=%06o\n\r",inode->i_mode);
	return -EINVAL;
}
