/**
 *	futex.c
 *
 *	Copyright (C) 2014 YangLing(yl.tienon@gmail.com)
 *
 *	Description:
 *
 *	Revision History:
 *
 *	2015-05-12 Created By YangLing
 */

#ifndef lint
static const char rcsid[] =
	"@(#) $Id: futex.c,"
	"v 1.00 2015/05/12 09:00:00 CST yangling Exp $ (LBL)";
#endif

#ifdef ENABLE_BUG_VERIFY
#pragma message	("enable bug verify")
#endif

#include "./list.h"
#include "./spinlock.h"
#include "futex/futex.h"
#include <stdio.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <windows.h>

#ifndef	always_inline
#define	always_inline	__forceinline
#endif

typedef unsigned int	magic_t;
#define	MAGIC_TASK_HI	((magic_t)(0x79616E67))
#define	MAGIC_TASK_LO	((magic_t)(0x6C696E67))
typedef struct task_s
{
	magic_t					magic_lo;
	/**
	 *	tsk_list
	 *	1:	挂接在 futex 等待队列上
	 *	2:	挂接在 空闲 队列上
	 *	3:	悬空
	 */
	struct list_head	tsk_list;
	
	/**
	 *	tsk_heap
	 *	0:	全局静态数据
	 *	1:	heap动态数据
	 */
	unsigned int			tsk_heap:1;
	unsigned int			_padding:31;
	HANDLE					tsk_evt;	/*	任务的事件句柄*/
	DWORD					tsk_tid;	/*	任务的线程ID值*/
	magic_t					magic_hi;
} task_t;

#define	is_invalid_task(_task_)						\
	(												\
		((_task_)->magic_lo != MAGIC_TASK_LO) ||	\
		((_task_)->magic_hi != MAGIC_TASK_HI)		\
	)

#define	MAGIC_FUTEX_HI	((magic_t)(0x7A686F75))
#define	MAGIC_FUTEX_LO	((magic_t)(0x70696E67))
typedef struct futex_s
{
	magic_t					magic_lo;
	/**
	 *	ftx_head
	 *	正在等待本 futex 的 task 链表头
	 */
	struct list_head		ftx_head;

	/**
	 *	ftx_list
	 *	1:	挂接在 hash 表中
	 *	2:	挂接在 空闲队列中
	 */
	struct list_head		ftx_list;

	/**
	 *	控制 ftx_head 链表的操作互斥
	 */
	CRITICAL_SECTION		ftx_lock;
	int			*			ftx_addr;

	/**
	 *	ftx_heap
	 *	0:	全局静态数据
	 *	1:	heap动态数据
	 */
	unsigned int			ftx_heap:1;
	unsigned int			ftx_refs:31;
	magic_t					magic_hi;
} futex_t;

#define	is_invalid_futex(_futex_)					\
	(												\
		((_futex_)->magic_lo != MAGIC_FUTEX_LO) ||	\
		((_futex_)->magic_hi != MAGIC_FUTEX_HI)		\
	)

static struct task_module
{
#define	NR_STATIC_TASKS		128
	task_t					tasks[NR_STATIC_TASKS];
	struct list_head		idle;
	spinlock_t				lock;
}_static_task_mod;

static struct futex_module
{
#define	FUTEX_HASH_SIZE		1024
#define NR_STATIC_FUTEXS	128
	futex_t					futexs[NR_STATIC_FUTEXS];
	struct list_head		hash[FUTEX_HASH_SIZE];
	struct list_head		idle;
	CRITICAL_SECTION		lock;
}_static_futex_mod;

static	DWORD				_static_tls_index;

/**
 *	libfutex_panic - 惊恐
 */
static void libfutex_panic(void)
{
	while(1)
	{
		assert(0);
		Sleep(1);
	}
}

/**
 *	task_init_once - task 描述符 一次性初始化
 *
 *	@task:			任务描述符
 *	@heap:			!0 从堆分配数据 0 全局静态数据
 *
 *	return
 *		无
 */
static void task_init_once(task_t * task, int heap)
{
	task->magic_hi	= MAGIC_TASK_HI;
	task->magic_lo	= MAGIC_TASK_LO;
	task->tsk_evt	= NULL;
	task->tsk_tid	= 0;
	task->tsk_heap	= ((heap) ? 1 : 0);
	task->_padding	= 0;
	INIT_LIST_HEAD(&(task->tsk_list));
}

/**
 *	task_link_event - task 关联 自动事件对象
 *
 *	@task:			任务描述符
 *
 *	return
 *		==	0		成功
 *		!=	0		失败
 */
static int task_link_event(task_t * task)
{
	/**
	 *	创建匿名自动事件内核对象,且初始化为无信号状态
	 */
	task->tsk_evt = CreateEvent(
			NULL, FALSE, FALSE, NULL);
	if(NULL == task->tsk_evt)
		return E_FUTEX_EVENT;
	else
		return E_FUTEX_DONE;
}

/**
 *	task_heap_new - 从 heap 分配任务描述符
 *
 *	return
 *		!=	NULL	成功
 *		==	NULL	失败
 */
static task_t * task_heap_new(void)
{
	task_t	*	task;

	task = (task_t *)malloc(sizeof(task_t));
	if(NULL != task)
	{
		task_init_once(task, 1);
	}

	return task;
}

/**
 *	task_idle_new - 从 idle 分配任务描述符
 *
 *	return
 *		!=	NULL	成功
 *		==	NULL	失败
 */
static task_t * task_idle_new(void)
{
	struct list_head * next = NULL;
	struct list_head * head = &(_static_task_mod.idle);
	spinlock_t		 * lock = &(_static_task_mod.lock);

	spin_acquire(lock);
	if(!list_empty(head))
	{
		next = head->next;
		list_del_init(next);
	}
	spin_release(lock);

	if(NULL != next)
	{
		return list_entry(next, task_t, tsk_list);
	}
	else
	{
		return NULL;
	}
}

/**
 *	task_release - 释放 task 描述符
 *
 *	@task:			任务描述符
 *
 *	return
 *		无
 */
static void task_release(task_t * task)
{
	struct list_head * list = &(task->tsk_list);
	struct list_head * head = &(_static_task_mod.idle);
	spinlock_t		 * lock = &(_static_task_mod.lock);

#ifdef ENABLE_BUG_VERIFY
	if(!list_empty(list))
		libfutex_panic();
#endif

	spin_acquire(lock);
	list_add(list, head);
	spin_release(lock);
}

/**
 *	task_acquire - 获得 task 描述符
 *
 *	return
 *		!=	NULL	成功
 *		==	NULL	失败
 */
static task_t * task_acquire(void)
{
	task_t * task;

	task = task_idle_new();
	if(NULL == task)
	{
		task = task_heap_new();
	}

	if(NULL != task)
		task->tsk_tid = GetCurrentThreadId();

	if(NULL != task && NULL == task->tsk_evt)
	{
		if(task_link_event(task))
		{
			task_release(task);
			return NULL;
		}
	}

	return task;	
}

/**
 *	who_am_i - 从 TLS 获取自己的描述符
 *
 *	return
 *		!=	NULL	成功
 *		==	NULL	失败
 */
static task_t * who_am_i(void)
{
	task_t	*	task;

	task = (task_t *)TlsGetValue(_static_tls_index);
	if(NULL != task)
	{
		return task;
	}

	task = task_acquire();
	if(NULL == task)
		return NULL;

	if(TlsSetValue(_static_tls_index, task))
	{
		return task;
	}
	else
	{
		task_release(task);
		return NULL;
	}
}

always_inline DWORD
calc_relmillisecs(const struct timespec * abstime)
{
	const __int64 NANOSEC_PER_MILLISEC = 1000000;
	const __int64 MILLISEC_PER_SEC = 1000;
	DWORD milliseconds;
	__int64 tmpAbsMilliseconds;
	__int64 tmpCurrMilliseconds;
	struct _timeb currSysTime;

	/**
	 * Calculate timeout as milliseconds from current system time. 
	 */
	
	/**
	 * subtract current system time from abstime in a way that checks
	 * that abstime is never in the past, or is never equivalent to the
	 * defined INFINITE value (0xFFFFFFFF).
	 *
	 * Assume all integers are unsigned, i.e. cannot test if less than 0.
	 */
	tmpAbsMilliseconds =  (__int64)abstime->tv_sec * MILLISEC_PER_SEC;
	tmpAbsMilliseconds += ((__int64)abstime->tv_nsec + (NANOSEC_PER_MILLISEC/2)) / NANOSEC_PER_MILLISEC;

	/* get current system time */
	_ftime(&currSysTime);

	tmpCurrMilliseconds = (__int64) currSysTime.time * MILLISEC_PER_SEC;
	tmpCurrMilliseconds += (__int64) currSysTime.millitm;

	if (tmpAbsMilliseconds > tmpCurrMilliseconds)
    {
		milliseconds = (DWORD) (tmpAbsMilliseconds - tmpCurrMilliseconds);
		if (milliseconds == INFINITE)
        {
			/* Timeouts must be finite */
			milliseconds--;
        }
    }
	else
    {
		/* The abstime given is in the past */
		milliseconds = 0;
    }

	return milliseconds;
}

/**
 *	task_sched - 任务指定超时时间调度
 *
 *	@task:			任务上下文描述指针
 *	@tmout:			超时时间(绝对时间)
 *
 *	return
 *		==	0		成功
 *		!=	0		失败
 */
static int task_sched(
	const task_t * task, const struct timespec * tmout)
{
	DWORD	result;
	DWORD	waittm;

	waittm = ((NULL == tmout) ? INFINITE :
				calc_relmillisecs(tmout));
	result = WaitForSingleObject(task->tsk_evt, waittm);
	switch(result)
	{
	case WAIT_OBJECT_0:
		return E_FUTEX_DONE;
	case WAIT_TIMEOUT:
		return E_FUTEX_TIMEOUT;
	case WAIT_ABANDONED:
		return E_FUTEX_PANIC;
	default:
		return E_FUTEX_FAILED;
	}
}

/**
 *	futex_hash - 计算指定 addr 的 HASH 链表头
 *
 *	@addr:			futex 地址
 *
 *	return
 *		futex 地址 对应的 HASH 链表头
 */
always_inline
static struct list_head * futex_hash(int * addr)
{
#ifdef WIN32
	unsigned int	v = (unsigned int)addr;
	unsigned char * p = (unsigned char*)&v;
	unsigned int	c = p[0];

	c = c * 131 + p[1];
	c = c * 131 + p[2];
	c = c * 131 + p[3];
#else
	__int64			v = (__int64)addr;
	unsigned char * p = (unsigned char*)&v;
	unsigned int	c = p[0];
	
	c = c * 131 + p[1];
	c = c * 131 + p[2];
	c = c * 131 + p[3];
	c = c * 131 + p[4];
	c = c * 131 + p[5];
	c = c * 131 + p[6];
	c = c * 131 + p[7];
#endif
	return _static_futex_mod.hash + (c % FUTEX_HASH_SIZE);
}

/**
 *	futex_init_once - futex 描述符 一次性初始化
 *
 *	@futex:			futex 描述符
 *	@heap:			!0 从堆分配数据 0 全局静态数据		
 *
 *	return
 *		无
 */
static void futex_init_once(futex_t * futex, int heap)
{
	futex->magic_hi	= MAGIC_FUTEX_HI;
	futex->magic_lo = MAGIC_FUTEX_LO;
	futex->ftx_refs	= 0;
	futex->ftx_heap	= ((heap) ? 1 : 0);
	futex->ftx_addr	= NULL;
	InitializeCriticalSection(&(futex->ftx_lock));
	INIT_LIST_HEAD(&(futex->ftx_head));
	INIT_LIST_HEAD(&(futex->ftx_list));
}

/**
 *	futex_heap_new - 从 heap 分配 futex 描述符
 *
 *	return
 *		!=	NULL	成功
 *		==	NULL	失败
 */
static futex_t * futex_heap_new(void)
{
	futex_t * futex;

	futex = malloc(sizeof(futex_t));
	if(NULL != futex)
	{
		futex_init_once(futex, 1);
	}

	return futex;
}

/**
 *	futex_idle_new - 从 idle 分配 futex 描述符
 *
 *	return
 *		!=	NULL	成功
 *		==	NULL	失败
 */
static futex_t * futex_idle_new(void)
{
	struct list_head * next;
	struct list_head * head = &(_static_futex_mod.idle);

	if(!list_empty(head))
	{
		next = head->next;
		list_del_init(next);

		return list_entry(next, futex_t, ftx_list);
	}
	else
		return NULL;
}

/**
 *	futex_acquire - 获得 futex 描述符
 *
 *	@addr:			futex 地址
 *
 *	return
 *		!=	NULL	成功
 *		==	NULL	失败
 */
static futex_t * futex_acquire(int * addr)
{
	futex_t * futex;
	struct list_head * head;

	futex = futex_idle_new();
	if(NULL == futex)
		futex = futex_heap_new();

	if(NULL != futex)
	{
		futex->ftx_addr = addr;
		++futex->ftx_refs;
		head = futex_hash(addr);
		list_add(&(futex->ftx_list), head);
	}

#ifdef ENABLE_BUG_VERIFY
	if(is_invalid_futex(futex))
		libfutex_panic();
	if(futex->ftx_refs != 1)
		libfutex_panic();
	if(!list_empty(&(futex->ftx_head)))
		libfutex_panic();
	if(list_empty(&(futex->ftx_list)))
		libfutex_panic();
	if(futex->ftx_addr != addr)
		libfutex_panic();
#endif

	return futex;
}

/**
 *	futex_release - 释放 futex 描述符
 *
 *	@futex:			futex 描述符
 *
 *	return
 *		无
 */
static void futex_release(futex_t * futex)
{
	struct list_head * list = &(futex->ftx_list);
	struct list_head * head = &(_static_futex_mod.idle);

#ifdef ENABLE_BUG_VERIFY
	if(futex->ftx_refs == 0)
		libfutex_panic();
#endif

	if(--futex->ftx_refs == 0)
	{
#ifdef ENABLE_BUG_VERIFY
		if(is_invalid_futex(futex))
			libfutex_panic();
		if(!list_empty(&(futex->ftx_head)))
			libfutex_panic();
		if(list_empty(&(futex->ftx_list)))
			libfutex_panic();
#endif
		/*	no body reference*/
		/*	remove from hash table*/
		list_del_init(list);

		/*	insert into idle list*/
		list_add(list, head);
	}
}

/**
 *	futex_find - 根据指定 addr 查找 futex 描述符
 *
 *	@addr:			futex 地址
 *
 *	return
 *		!=	NULL	成功
 *		==	NULL	失败
 */
static futex_t * futex_find(int * addr)
{
	struct list_head * head;
	struct list_head * list;
	futex_t		*	futex;

	head = futex_hash(addr);

	list_for_each(list, head)
	{
		futex = list_entry(list, futex_t, ftx_list);
		if(futex->ftx_addr == addr)
		{
			++futex->ftx_refs;
			return futex;
		}
	}

	return NULL;
}

/**
 *	futex_lock - 锁定 futex 描述符
 *
 *	@futex:			futex 描述符
 *
 *	return
 *		无
 */
always_inline static void futex_lock(futex_t * futex)
{
	EnterCriticalSection(&(futex->ftx_lock));
}

/**
 *	futex_lock - 解锁 futex 描述符
 *
 *	@futex:			futex 描述符
 *
 *	return
 *		无
 */
always_inline static void futex_unlock(futex_t * futex)
{
	LeaveCriticalSection(&(futex->ftx_lock));
}

/**
 *	futex_ref - 根据指定 addr 引用 futex 描述符
 *
 *	@addr:			futex 地址
 *
 *	return
 *		!=	NULL	成功 ( futex 描述符 )
 *		==	NULL	失败 ( 未匹配到 )
 */
static futex_t * futex_ref(int * addr)
{
	futex_t * futex;
	CRITICAL_SECTION * lock = &(_static_futex_mod.lock);

	EnterCriticalSection(lock);
	futex = futex_find(addr);
	LeaveCriticalSection(lock);
	
	return futex;
}

/**
 *	futex_get - 根据指定 addr 引用 futex 描述符
 *
 *	@addr:			futex 地址
 *
 *	return
 *		!=	NULL	成功 ( futex 描述符 )
 *		==	NULL	失败 ( ENOMEM )
 */
static futex_t * futex_get(int * addr)
{
	futex_t * futex;
	CRITICAL_SECTION * lock = &(_static_futex_mod.lock);
	
	EnterCriticalSection(lock);
	futex = futex_find(addr);
	if(NULL == futex)
		futex = futex_acquire(addr);
	LeaveCriticalSection(lock);
	
	return futex;
}

/**
 *	futex_put - 归还 futex 描述符
 *
 *	@futex:			futex 描述符
 *
 *	return
 *		无
 */
static void futex_put(futex_t * futex)
{
	CRITICAL_SECTION * lock = &(_static_futex_mod.lock);

	EnterCriticalSection(lock);
	futex_release(futex);
	LeaveCriticalSection(lock);
}

/**
 *	futex_wait_slow - futex 等待 慢速操作过程
 *
 *	@futex:			futex 描述符
 *	@self:			线程自己的 task 描述符
 *	@tmout:			超时时间(绝对时间)
 *
 *	return
 *		==	0		成功
 *		!=	0		失败
 */
static int futex_wait_slow(
	futex_t * futex, task_t * self,
	const struct timespec * tmout)
{
	int result;
	int rewait;
	struct list_head * list = &(self->tsk_list);

	result = task_sched(self, tmout);
	if(E_FUTEX_DONE != result)
	{
		rewait = 0;
		futex_lock(futex);
		if(result == E_FUTEX_TIMEOUT && list_empty(list))
		{
			rewait = 1;
		}
		list_del_init(list);
		futex_unlock(futex);

		/**
		 *	其它线程已经唤醒了自己再次进入等待
		 *	以便保持 self->task_evt 信号一致性
		 */
		if(rewait)
			result = task_sched(self, NULL);
	}

#ifdef ENABLE_BUG_VERIFY
	if(is_invalid_task(self))
		libfutex_panic();
	if(!list_empty(&(self->tsk_list)))
		libfutex_panic();
	if(self->tsk_tid != GetCurrentThreadId())
		libfutex_panic();
#endif

	return result;
}

/**
 *	futex_wait - 原子判断 val 与 *addr 相等 则等待 否则不等待
 *
 *	@addr:			futex 地址
 *	@val:			判断变量值
 *	@tmout:			超时时间(绝对时间)
 *
 *	return
 *		==	0		成功
 *		!=	0		失败
 */
static int
futex_wait(int * addr, int val,
	const struct timespec * tmout)
{
	int	result;
	task_t * self;
	futex_t	* futex;

	result = IsBadReadPtr(addr, sizeof(int));
	if(result)
		return E_FUTEX_ACCESS;

	self = who_am_i();
	if(NULL == self)
		return E_FUTEX_WHOAMI;

	futex = futex_get(addr);
	if(NULL == futex)
		return E_FUTEX_NOMEM;

#ifdef ENABLE_BUG_VERIFY
	if(!list_empty(&(self->tsk_list)))
		libfutex_panic();
	if(self->tsk_tid != GetCurrentThreadId())
		libfutex_panic();
	if(self->tsk_evt == NULL)
		libfutex_panic();
	if(is_invalid_task(self))
		libfutex_panic();
#endif

	futex_lock(futex);
	if(*(volatile int *)addr != val)
	{
		futex_unlock(futex);
		futex_put(futex);
		return 0;
	}
	else
	{
		list_add_tail(
			&(self->tsk_list), &(futex->ftx_head));
		futex_unlock(futex);
		result = futex_wait_slow(futex, self, tmout);
		futex_put(futex);
		return result;
	}
}

/**
 *	futex_wake_fast - 快速唤醒指定数量等待在 futex 上的线程
 *
 *	@futex:			futex 描述符
 *	@nr_wake:		唤醒的线程数量
 *
 *	return
 *		成功唤醒的线程数量
 */
static int futex_wake_fast(futex_t * futex, int nr_wake)
{
	struct list_head *curr;
	struct list_head *next;
	struct list_head *head = &(futex->ftx_head);
	task_t * task;
	int result = 0;

	futex_lock(futex);

	list_for_each_safe(curr, next, head)
	{
		list_del_init(curr);
		task = list_entry(curr, task_t, tsk_list);
#ifdef ENABLE_BUG_VERIFY
		if(!SetEvent(task->tsk_evt))
			libfutex_panic();
#else
		(void) SetEvent(task->tsk_evt);
#endif
		
		if(++result >= nr_wake)
			break;
	}

	futex_unlock(futex);

	return result;
}

/**
 *	futex_wake - 唤醒指定数量等待在 futex 上的线程
 *
 *	@addr:			futex 地址
 *	@nr_wake:		唤醒的线程数量
 *
 *	return
 *		成功唤醒的线程数量
 */
static int futex_wake(int * addr, int nr_wake)
{
	futex_t	* futex;
	int result = 0;

	futex = futex_ref(addr);
	if(NULL == futex)
		return result;

	result = futex_wake_fast(futex, nr_wake);

	futex_put(futex);

	return result;
}

/**
 *	futex - 用户互斥体仲裁操作
 *
 *	@addr:			futex 地址
 *	@op:			操作类型
 *	@val:			仲裁值
 *	@tmout:			超时时间(绝对时间)
 *
 *	A. semantics:
 *
 *	FUTEX_WAIT
 *		if( *addr != val)
 *			goto sleep;
 *		else
 *			continue;
 *
 *	FUTEX_WAKE
 *		for(i = 0; i < val; i++)
 *			wakeup thread;
 *
 *	B. description:
 *		futex 函数为程序提供一个等待指定地址的值改变的方法.
 *	另外也提供一个唤醒等待在指定地址的所有线程的方法.它通常
 *	用于实现线程间共享变量的锁的竞争情况.
 *
 *		当线程间的共享变量存在无法仲裁的锁竞争关系的时候,可
 *	调用 futex 函数来仲裁是否睡眠或者是否唤醒线程.
 *
 *	@addr 参数必须是一个指向32bit对齐值的指针, @op 参数决定
 *	@val 参数的含义
 *
 *	当前定义了2个 @op 行为
 *	FUTEX_WAIT
 *		本操作原子的验证 *uaddr 与 @val 的值 是否相等,如果不
 *	相等,则进入睡眠状态.直到 FUTEX_WAKE 操作或者发生超时情况
 *	如果相等的话在本函数直接返回.
 *
 *	FUTEX_WAKE
 *		本函数唤醒最多 @val 等待在 uaddr 上的线程 tmout 参数
 *	将被忽略.
 *
 *	C. return:
 *		返回值的含义将依赖 @op 的类型
 *
 *	FUTEX_WAIT
 *		返回0 意味着 被 FUTEX_WAKE 唤醒 或者 是 *uaddr != @val
 *	返回 -E_FUTEX_TIMEOUT 意味着 超时 其它任何返回值意味着发生
 *	错误.
 *
 *	FUTEX_WAKE
 *		返回被唤醒的线程数量
 *
 *	D. errors:
 *		E_FUTEX_WHOAMI		无法获取自己
 *		E_FUTEX_NOMEM		内存不足
 *		E_FUTEX_PANIC		WFSO 异常返回
 *		E_FUTEX_TIMEOUT		WFSO 超时返回
 *		E_FUTEX_FAILED		WFSO 错误返回
 *		E_FUTEX_EVENT		无法创建自动事件句柄
 *		E_FUTEX_INVAL		op 参数错误
 *		E_FUTEX_ACCESS		*addr 指向的地址不可读
 */
LIBFUTEX_API int futex(
	int * addr, int op, int val,
	const struct timespec *tmout)
{
	int result;

	switch(op)
	{
	case FUTEX_WAIT:
		result = -futex_wait(addr, val, tmout);
		break;
	case FUTEX_WAKE:
		result = futex_wake(addr, val);
		break;
	default:
		result = -E_FUTEX_INVAL;
	}

	return result;
}

LIBFUTEX_API void xxxx(void)
{
	task_t * task1, *task2;
	task1 = task_acquire();
	task2 = task_acquire();
	
	task_release(task2);
	task_release(task1);
	
	task1 = task_acquire();
	task2 = task_acquire();
	
}

/**
 *	do_task_module_init - task 模块初始化
 */
static void do_task_module_init(void)
{
	int	i;
	task_t * task;
	struct list_head* _idle = &(_static_task_mod.idle);
	task_t			* _data = _static_task_mod.tasks;
	spinlock_t		* _lock = &(_static_task_mod.lock);
	
	spin_init(_lock);
	INIT_LIST_HEAD(_idle);

	for(i = 0; i < NR_STATIC_TASKS; i++)
	{
		task = _data + i;
		task_init_once(task, 0);
		list_add_tail(&(task->tsk_list), _idle);
	}

}

/**
 *	do_futex_module_init - futex 模块初始化
 */
static void do_futex_module_init(void)
{
	int i;
	futex_t * futex;
	struct list_head* _idle = &(_static_futex_mod.idle);
	futex_t			* _data = _static_futex_mod.futexs;
	CRITICAL_SECTION* _lock = &(_static_futex_mod.lock);
	struct list_head* _hash = _static_futex_mod.hash;

	InitializeCriticalSection(_lock);
	INIT_LIST_HEAD(_idle);

	for(i = 0; i < FUTEX_HASH_SIZE; i++)
	{
		INIT_LIST_HEAD(&(_hash[i]));
	}

	for(i = 0; i < NR_STATIC_FUTEXS; i++)
	{
		futex = _data + i;
		futex_init_once(futex, 0);
		list_add_tail(&(futex->ftx_list), _idle);
	}

}

/**
 *	do_process_attach - 处理 DLL_PROCESS_ATTACH 消息
 *
 *	return
 *		==	TRUE	成功
 *		==	FALSE	失败
 */
static BOOL do_process_attach(void)
{
	do_task_module_init();
	do_futex_module_init();

	_static_tls_index = TlsAlloc();

	return ((_static_tls_index !=
			(DWORD)(-1)) ? TRUE : FALSE);
}

/**
 *	do_process_detach - 处理 DLL_PROCESS_DETACH 消息
 *
 *	return
 *		==	TRUE	成功
 *		==	FALSE	失败
 */
static BOOL do_process_detach(void)
{
	return TRUE;
}

/**
 *	do_thread_attach - 处理 DLL_THREAD_ATTACH 消息
 *
 *	return
 *		==	TRUE	成功
 *		==	FALSE	失败
 */
static BOOL do_thread_attach(void)
{
	return TRUE;
}

/**
 *	do_thread_detach - 处理 DLL_THREAD_DETACH 消息
 *
 *	return
 *		==	TRUE	成功
 *		==	FALSE	失败
 */
static BOOL do_thread_detach(void)
{
	task_t * task;
	
	task = (task_t*)TlsGetValue(_static_tls_index);
	if(NULL != task)
	{
		task_release(task);
		(void) TlsSetValue(_static_tls_index, NULL);
	}

	return TRUE;
}

BOOL WINAPI DllMain(
					HINSTANCE	hinstDLL,
					DWORD		fdwReason,
					LPVOID		lpvReserved)
{
	BOOL result = TRUE;

	switch(fdwReason)
	{
	case DLL_PROCESS_ATTACH:
		// Initialize once for each new process.
		// Return FALSE to fail DLL load.
		result = do_process_attach();
		break;

	case DLL_THREAD_ATTACH:
		// Do thread-specific initialization.
		result = do_thread_attach();
		break;

	case DLL_THREAD_DETACH:
		// Do thread-specific cleanup.
		result = do_thread_detach();
		break;

	case DLL_PROCESS_DETACH:
		// Perform any necessary cleanup.
		result = do_process_detach();
		break;
    }

	return result;
}
