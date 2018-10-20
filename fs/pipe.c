/*
 *  linux/fs/pipe.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <signal.h>

#include <linux/sched.h>
#include <linux/mm.h>	/* for get_free_page */
#include <asm/segment.h>

int read_pipe(struct m_inode * inode, char * buf, int count)
{
	int chars, size, read = 0;

	while (count>0) {
		while (!(size=PIPE_SIZE(*inode))) { //파이프에 데이터가 있으면, 
			wake_up(&inode->i_wait);//write 를 runnable로
			if (inode->i_count != 2) /* are there any writers? */
				return read;//쓰는프로세스가 없으면 리턴
			sleep_on(&inode->i_wait);// 파이프를 읽는 프로세스가 대기 상태가 된다.
                                     // 파이프에 데이터를 쓰는 프로세스로 전환된다.
                                     // schedule에서 write가 깨어남
		}
		chars = PAGE_SIZE-PIPE_TAIL(*inode);// 오버플로우 전까지 읽을 수 있는양
		if (chars > count)// 읽어야 하는 양이 더 작은경우(오버플로우 아님)
			chars = count;
        else
        {
        }    // 오버플로우 발생

		if (chars > size)// size: 쓰여져있는 값이 더 작은경우(오버플로우 아님)
			chars = size;
        else
        {   // 오버플로우 발생
        }
        
		count -= chars;// 다음번에 읽을값
		read += chars;
		size = PIPE_TAIL(*inode);//읽을 위치
		PIPE_TAIL(*inode) += chars;//읽은 후의 위치
		PIPE_TAIL(*inode) &= (PAGE_SIZE-1);//오버플로우 보정
        // if(PIPE_TAIL(*inode) == (PAGE_SIZE)) 
        //    PIPE_TAIL(*inode) = 0 이 된다
		while (chars-->0)// chars: 이시점에서 읽을 값
			put_fs_byte(((char *)inode->i_size)[size++],buf++);
	}
	wake_up(&inode->i_wait);
	return read;
}
	
int write_pipe(struct m_inode * inode, char * buf, int count)
{
	int chars, size, written = 0;

	while (count>0) {
		while (!(size=(PAGE_SIZE-1)-PIPE_SIZE(*inode))) {//쓸 공간을 계산
			wake_up(&inode->i_wait);// read 를 깨워준다
			if (inode->i_count != 2) { /* no readers */
				current->signal |= (1<<(SIGPIPE-1));// write_pipe에만 signal이 있고 read_pipe에는 없다
				return written?written:-1;
			}
			sleep_on(&inode->i_wait);
		}
        // read_pipe 와 유사한 처리
		chars = PAGE_SIZE-PIPE_HEAD(*inode);//head > tail 
		if (chars > count)
			chars = count;
		if (chars > size)
			chars = size;
		count -= chars;
		written += chars;
		size = PIPE_HEAD(*inode);
		PIPE_HEAD(*inode) += chars;
		PIPE_HEAD(*inode) &= (PAGE_SIZE-1);
		while (chars-->0)
			((char *)inode->i_size)[size++]=get_fs_byte(buf++);
	}
	wake_up(&inode->i_wait);
	return written;
}

int sys_pipe(unsigned long * fildes)
{
	struct m_inode * inode;
	struct file * f[2];//input/output 이라 두개
	int fd[2];
	int i,j;

	j=0;
	for(i=0;j<2 && i<NR_FILE;i++) // file_table[64]에서 두 개의 아이템을 찾는다
		if (!file_table[i].f_count)// 사용하지 않는 아이템을 찾는다
			(f[j++]=i+file_table)->f_count++; // f_count에 1을 설정한다
	if (j==1)// 예외처리,위에서 증가시킨거 원복
		f[0]->f_count=0;
	if (j<2)// 예외처리, j=0, 1 일때 실패
		return -1;
	j=0;
	for(i=0;j<2 && i<NR_OPEN;i++)//현재 프로세스에 파일포인터 빈것을 찾는다
		if (!current->filp[i]) {
			current->filp[ fd[j]=i ] = f[j];
			j++;
		}
	if (j==1)//원복
		current->filp[fd[0]]=NULL;
	if (j<2) {//예외처리
		f[0]->f_count=f[1]->f_count=0;
		return -1;
	}
	if (!(inode=get_pipe_inode())) {// 파이프 파일의 i-node 생성
		current->filp[fd[0]] =
			current->filp[fd[1]] = NULL;
		f[0]->f_count = f[1]->f_count = 0;
		return -1;
	}
	f[0]->f_inode = f[1]->f_inode = inode;
	f[0]->f_pos = f[1]->f_pos = 0;
	f[0]->f_mode = 1;		/* read */
	f[1]->f_mode = 2;		/* write */
	put_fs_long(fd[0],0+fildes);//읽기 파이프 파일 핸들러 설정(커널->유저영역)
	put_fs_long(fd[1],1+fildes);//쓰기 파이프 파일 핸들러 설정
	return 0;
}
