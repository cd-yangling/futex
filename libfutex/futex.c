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
	 *	1:	�ҽ��� futex �ȴ�������
	 *	2:	�ҽ��� ���� ������
	 *	3:	����
	 */
	struct list_head	tsk_list;
	
	/**
	 *	tsk_heap
	 *	0:	ȫ�־�̬����
	 *	1:	heap��̬����
	 */
	unsigned int			tsk_heap:1;
	unsigned int			_padding:31;
	HANDLE					tsk_evt;	/*	������¼����*/
	DWORD					tsk_tid;	/*	������߳�IDֵ*/
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
	 *	���ڵȴ��� futex �� task ����ͷ
	 */
	struct list_head		ftx_head;

	/**
	 *	ftx_list
	 *	1:	�ҽ��� hash ����
	 *	2:	�ҽ��� ���ж�����
	 */
	struct list_head		ftx_list;

	/**
	 *	���� ftx_head ����Ĳ�������
	 */
	CRITICAL_SECTION		ftx_lock;
	int			*			ftx_addr;

	/**
	 *	ftx_heap
	 *	0:	ȫ�־�̬����
	 *	1:	heap��̬����
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
 *	libfutex_panic - ����
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
 *	task_init_once - task ������ һ���Գ�ʼ��
 *
 *	@task:			����������
 *	@heap:			!0 �Ӷѷ������� 0 ȫ�־�̬����
 *
 *	return
 *		��
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
 *	task_link_event - task ���� �Զ��¼�����
 *
 *	@task:			����������
 *
 *	return
 *		==	0		�ɹ�
 *		!=	0		ʧ��
 */
static int task_link_event(task_t * task)
{
	/**
	 *	���������Զ��¼��ں˶���,�ҳ�ʼ��Ϊ���ź�״̬
	 */
	task->tsk_evt = CreateEvent(
			NULL, FALSE, FALSE, NULL);
	if(NULL == task->tsk_evt)
		return E_FUTEX_EVENT;
	else
		return E_FUTEX_DONE;
}

/**
 *	task_heap_new - �� heap ��������������
 *
 *	return
 *		!=	NULL	�ɹ�
 *		==	NULL	ʧ��
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
 *	task_idle_new - �� idle ��������������
 *
 *	return
 *		!=	NULL	�ɹ�
 *		==	NULL	ʧ��
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
 *	task_release - �ͷ� task ������
 *
 *	@task:			����������
 *
 *	return
 *		��
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
 *	task_acquire - ��� task ������
 *
 *	return
 *		!=	NULL	�ɹ�
 *		==	NULL	ʧ��
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
 *	who_am_i - �� TLS ��ȡ�Լ���������
 *
 *	return
 *		!=	NULL	�ɹ�
 *		==	NULL	ʧ��
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
 *	task_sched - ����ָ����ʱʱ�����
 *
 *	@task:			��������������ָ��
 *	@tmout:			��ʱʱ��(����ʱ��)
 *
 *	return
 *		==	0		�ɹ�
 *		!=	0		ʧ��
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
 *	futex_hash - ����ָ�� addr �� HASH ����ͷ
 *
 *	@addr:			futex ��ַ
 *
 *	return
 *		futex ��ַ ��Ӧ�� HASH ����ͷ
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
 *	futex_init_once - futex ������ һ���Գ�ʼ��
 *
 *	@futex:			futex ������
 *	@heap:			!0 �Ӷѷ������� 0 ȫ�־�̬����		
 *
 *	return
 *		��
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
 *	futex_heap_new - �� heap ���� futex ������
 *
 *	return
 *		!=	NULL	�ɹ�
 *		==	NULL	ʧ��
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
 *	futex_idle_new - �� idle ���� futex ������
 *
 *	return
 *		!=	NULL	�ɹ�
 *		==	NULL	ʧ��
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
 *	futex_acquire - ��� futex ������
 *
 *	@addr:			futex ��ַ
 *
 *	return
 *		!=	NULL	�ɹ�
 *		==	NULL	ʧ��
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
 *	futex_release - �ͷ� futex ������
 *
 *	@futex:			futex ������
 *
 *	return
 *		��
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
 *	futex_find - ����ָ�� addr ���� futex ������
 *
 *	@addr:			futex ��ַ
 *
 *	return
 *		!=	NULL	�ɹ�
 *		==	NULL	ʧ��
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
 *	futex_lock - ���� futex ������
 *
 *	@futex:			futex ������
 *
 *	return
 *		��
 */
always_inline static void futex_lock(futex_t * futex)
{
	EnterCriticalSection(&(futex->ftx_lock));
}

/**
 *	futex_lock - ���� futex ������
 *
 *	@futex:			futex ������
 *
 *	return
 *		��
 */
always_inline static void futex_unlock(futex_t * futex)
{
	LeaveCriticalSection(&(futex->ftx_lock));
}

/**
 *	futex_ref - ����ָ�� addr ���� futex ������
 *
 *	@addr:			futex ��ַ
 *
 *	return
 *		!=	NULL	�ɹ� ( futex ������ )
 *		==	NULL	ʧ�� ( δƥ�䵽 )
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
 *	futex_get - ����ָ�� addr ���� futex ������
 *
 *	@addr:			futex ��ַ
 *
 *	return
 *		!=	NULL	�ɹ� ( futex ������ )
 *		==	NULL	ʧ�� ( ENOMEM )
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
 *	futex_put - �黹 futex ������
 *
 *	@futex:			futex ������
 *
 *	return
 *		��
 */
static void futex_put(futex_t * futex)
{
	CRITICAL_SECTION * lock = &(_static_futex_mod.lock);

	EnterCriticalSection(lock);
	futex_release(futex);
	LeaveCriticalSection(lock);
}

/**
 *	futex_wait_slow - futex �ȴ� ���ٲ�������
 *
 *	@futex:			futex ������
 *	@self:			�߳��Լ��� task ������
 *	@tmout:			��ʱʱ��(����ʱ��)
 *
 *	return
 *		==	0		�ɹ�
 *		!=	0		ʧ��
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
		 *	�����߳��Ѿ��������Լ��ٴν���ȴ�
		 *	�Ա㱣�� self->task_evt �ź�һ����
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
 *	futex_wait - ԭ���ж� val �� *addr ��� ��ȴ� ���򲻵ȴ�
 *
 *	@addr:			futex ��ַ
 *	@val:			�жϱ���ֵ
 *	@tmout:			��ʱʱ��(����ʱ��)
 *
 *	return
 *		==	0		�ɹ�
 *		!=	0		ʧ��
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
 *	futex_wake_fast - ���ٻ���ָ�������ȴ��� futex �ϵ��߳�
 *
 *	@futex:			futex ������
 *	@nr_wake:		���ѵ��߳�����
 *
 *	return
 *		�ɹ����ѵ��߳�����
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
 *	futex_wake - ����ָ�������ȴ��� futex �ϵ��߳�
 *
 *	@addr:			futex ��ַ
 *	@nr_wake:		���ѵ��߳�����
 *
 *	return
 *		�ɹ����ѵ��߳�����
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
 *	futex - �û��������ٲò���
 *
 *	@addr:			futex ��ַ
 *	@op:			��������
 *	@val:			�ٲ�ֵ
 *	@tmout:			��ʱʱ��(����ʱ��)
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
 *		futex ����Ϊ�����ṩһ���ȴ�ָ����ַ��ֵ�ı�ķ���.
 *	����Ҳ�ṩһ�����ѵȴ���ָ����ַ�������̵߳ķ���.��ͨ��
 *	����ʵ���̼߳乲����������ľ������.
 *
 *		���̼߳�Ĺ�����������޷��ٲõ���������ϵ��ʱ��,��
 *	���� futex �������ٲ��Ƿ�˯�߻����Ƿ����߳�.
 *
 *	@addr ����������һ��ָ��32bit����ֵ��ָ��, @op ��������
 *	@val �����ĺ���
 *
 *	��ǰ������2�� @op ��Ϊ
 *	FUTEX_WAIT
 *		������ԭ�ӵ���֤ *uaddr �� @val ��ֵ �Ƿ����,�����
 *	���,�����˯��״̬.ֱ�� FUTEX_WAKE �������߷�����ʱ���
 *	�����ȵĻ��ڱ�����ֱ�ӷ���.
 *
 *	FUTEX_WAKE
 *		������������� @val �ȴ��� uaddr �ϵ��߳� tmout ����
 *	��������.
 *
 *	C. return:
 *		����ֵ�ĺ��彫���� @op ������
 *
 *	FUTEX_WAIT
 *		����0 ��ζ�� �� FUTEX_WAKE ���� ���� �� *uaddr != @val
 *	���� -E_FUTEX_TIMEOUT ��ζ�� ��ʱ �����κη���ֵ��ζ�ŷ���
 *	����.
 *
 *	FUTEX_WAKE
 *		���ر����ѵ��߳�����
 *
 *	D. errors:
 *		E_FUTEX_WHOAMI		�޷���ȡ�Լ�
 *		E_FUTEX_NOMEM		�ڴ治��
 *		E_FUTEX_PANIC		WFSO �쳣����
 *		E_FUTEX_TIMEOUT		WFSO ��ʱ����
 *		E_FUTEX_FAILED		WFSO ���󷵻�
 *		E_FUTEX_EVENT		�޷������Զ��¼����
 *		E_FUTEX_INVAL		op ��������
 *		E_FUTEX_ACCESS		*addr ָ��ĵ�ַ���ɶ�
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
 *	do_task_module_init - task ģ���ʼ��
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
 *	do_futex_module_init - futex ģ���ʼ��
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
 *	do_process_attach - ���� DLL_PROCESS_ATTACH ��Ϣ
 *
 *	return
 *		==	TRUE	�ɹ�
 *		==	FALSE	ʧ��
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
 *	do_process_detach - ���� DLL_PROCESS_DETACH ��Ϣ
 *
 *	return
 *		==	TRUE	�ɹ�
 *		==	FALSE	ʧ��
 */
static BOOL do_process_detach(void)
{
	return TRUE;
}

/**
 *	do_thread_attach - ���� DLL_THREAD_ATTACH ��Ϣ
 *
 *	return
 *		==	TRUE	�ɹ�
 *		==	FALSE	ʧ��
 */
static BOOL do_thread_attach(void)
{
	return TRUE;
}

/**
 *	do_thread_detach - ���� DLL_THREAD_DETACH ��Ϣ
 *
 *	return
 *		==	TRUE	�ɹ�
 *		==	FALSE	ʧ��
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
