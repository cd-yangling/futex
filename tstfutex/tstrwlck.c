/**
 *	使用 libfutex 实现 rwlck
 *	rwlck 的实现参考
 *	glibc pthread_rwlock_xxxx family 函数
 */

#include "futex/futex.h"
#include "../libfutex/spinlock.h"
#include <stdio.h>
#include <string.h>
#include <process.h>
#include <limits.h>	/*	INT_MAX required*/
#include <windows.h>

typedef struct rwlck_s
{
	spinlock_t		slock;
	unsigned int	nr_readers;
	unsigned int	readers_wakeup;
	unsigned int	writer_wakeup;
	unsigned int	nr_readers_queued;
	unsigned int	nr_writers_queued;
	int				writer;
}rwlck_t;

static void rwlck_init(rwlck_t * rwlck)
{
	memset(rwlck, 0, sizeof(rwlck_t));
}

/**
 *	0: prefer readers
 *	1: perfer writer 
 */
static void rwlck_rdlock(rwlck_t * rwlck)
{
	int	_futex;

	spin_acquire(&rwlck->slock);

	/*	没有写者,且没有写等待 1 - 写优先逻辑*/
	if((rwlck->writer == 0) && 
		(rwlck->nr_writers_queued == 0))
	{
		++rwlck->nr_readers;
		spin_release(&rwlck->slock);
		return;
	}

	/*	有竞争关系,使用futex仲裁*/
	for(;;)
	{
		++rwlck->nr_readers_queued;

		_futex = rwlck->readers_wakeup;

		spin_release(&rwlck->slock);

		(void) futex(&rwlck->readers_wakeup, FUTEX_WAIT, _futex, NULL);

		spin_acquire(&rwlck->slock);

		--rwlck->nr_readers_queued;

		if((rwlck->writer == 0) && 
			(rwlck->nr_writers_queued == 0))
		{
			/*	没有写者,且没有写等待*/
			++rwlck->nr_readers;
			break;
		}
	}

	spin_release(&rwlck->slock);
}

static void rwlck_wrlock(rwlck_t * rwlck)
{
	DWORD	tid = GetCurrentThreadId();
	int		_futex;

	spin_acquire(&rwlck->slock);

	/*	没有写者 且 没有读者*/
	if(rwlck->writer == 0 && rwlck->nr_readers == 0)
	{
		rwlck->writer = tid;
		spin_release(&rwlck->slock);
		return;
	}

	/*	有竞争关系,使用futex仲裁*/
	for(;;)
	{
		++rwlck->nr_writers_queued;

		_futex = rwlck->writer_wakeup;

		spin_release(&rwlck->slock);

		(void) futex(&rwlck->writer_wakeup, FUTEX_WAIT, _futex, NULL);

		spin_acquire(&rwlck->slock);

		--rwlck->nr_writers_queued;
		/*	没有写者 且 没有读者*/
		if(rwlck->writer == 0 && rwlck->nr_readers == 0)
		{
			rwlck->writer = tid;
			break;
		}
	}
	spin_release(&rwlck->slock);
}

static void rwlck_unlock(rwlck_t * rwlck)
{
	spin_acquire(&rwlck->slock);

	if(rwlck->writer)
		rwlck->writer = 0;
	else
		--rwlck->nr_readers;
	if(rwlck->nr_readers == 0)
	{
		if(rwlck->nr_writers_queued)
		{
			/*	有写者 排队*/
			++rwlck->writer_wakeup;
			spin_release(&rwlck->slock);
			(void) futex(&rwlck->writer_wakeup, FUTEX_WAKE, 1, NULL);
			return;
		}
		else if(rwlck)
		{
			/*	有读者 排队*/
			++rwlck->readers_wakeup;
			spin_release(&rwlck->slock);
			(void) futex(&rwlck->readers_wakeup, FUTEX_WAKE, INT_MAX, NULL);
			return;
		}
	}

	spin_release(&rwlck->slock);
}

static rwlck_t			_rwlck;
static unsigned __int64	_value;

static void rwlck_thread(void * args)
{
	for(;;)
	{
		rwlck_wrlock(&_rwlck);
		_value++;
		rwlck_unlock(&_rwlck);
		rwlck_rdlock(&_rwlck);
		rwlck_unlock(&_rwlck);
	}
}

void rwlck_test1(void)
{
	_beginthread(rwlck_thread, 0, NULL);
	_beginthread(rwlck_thread, 0, NULL);
	_beginthread(rwlck_thread, 0, NULL);
	_beginthread(rwlck_thread, 0, NULL);

	for(;;)
	{
		fprintf(stdout, "%18I64u\n", _value);
		Sleep(1000);
	}

	Sleep(INFINITE);
}

