/**
 *	使用 libfutex 实现 mutex
 *	mutex 的实现参考
 *	http://dept-info.labri.fr/~denis/Enseignement/2008-IR/Articles/01-futex.pdf
 *	借用这个实现来对 libfutex 进行高频碰撞测试
 */

#include "futex/futex.h"
#include <stdio.h>
#include <process.h>
#include <windows.h>

__forceinline static int atomic_dec(int * minuend)
{
	__asm
	{
		mov			edx, dword ptr [minuend];
		mov			eax, 0xFFFFFFFF;
		lock xadd dword ptr [edx], eax;
	}
}

__forceinline static
unsigned int xchg(unsigned int * old_val, unsigned int new_val)
{
	_asm
	{
		mov			ecx, dword ptr [old_val];
		mov			eax, new_val;
		lock xchg	dword ptr [ecx], eax;
	}
}

__forceinline static
unsigned int cmpxchg(unsigned int * old_val, unsigned int cmp_val, unsigned int new_val)
{
	_asm
	{
		mov				ecx, dword ptr [old_val];
		mov				edx, new_val;
		mov				eax, cmp_val;
		lock cmpxchg	dword ptr [ecx],edx;
	}
}

typedef struct mutex_s
{
	int						futex;
} mutex_t;

static CRITICAL_SECTION		_wincs;
static mutex_t				_mutex;

#define	NR_TST_THREAD		4
#define	MAX_INC_VALUE		((unsigned __int64)(100000000))
static unsigned __int64		thread_value[NR_TST_THREAD];
static unsigned __int64		global_value;

static void mutex_lock(mutex_t * mutex)
{
	int		c;

	if((c = cmpxchg(&mutex->futex, 0, 1)) != 0)
	{
		if(c != 2)
			c = xchg(&mutex->futex, 2);
		
		while(c != 0)
		{
			futex(&mutex->futex, FUTEX_WAIT, 2, NULL);
			c = xchg(&mutex->futex, 2);
		}
	}
}

static void mutex_unlock(mutex_t * mutex)
{
	if(atomic_dec(&mutex->futex) != 1)
	{
		mutex->futex = 0;
		futex(&mutex->futex, FUTEX_WAKE, 1, NULL);
	}
}

static void mutex_thread(void * args)
{
	int	v = (int)args;
	unsigned __int64 i;

	for(i = 0; i < MAX_INC_VALUE; i++)
	{
		mutex_lock(&_mutex);
		thread_value[v]++;
		global_value++;
		mutex_unlock(&_mutex);
	}
}

static void wincs_thread(void * args)
{
	int	v = (int)args;
	unsigned __int64 i;
	
	for(i = 0; i < MAX_INC_VALUE; i++)
	{
		EnterCriticalSection(&_wincs);
		thread_value[v]++;
		global_value++;
		LeaveCriticalSection(&_wincs);
	}
}

void mutex_test1(int f)
{
	int		i;
	DWORD	Start;
	DWORD	Stop;
	const char *p;

	if(f)
		printf("wincs test\n");
	else
		printf("mutex test\n");

	if(f)
		InitializeCriticalSection(&_wincs);
	else
		_mutex.futex = 0;

	global_value = 0;
	memset(thread_value, 0, sizeof(thread_value));

	if(f)
	{
		for(i = 0; i < NR_TST_THREAD; i++)
		{
			_beginthread(mutex_thread, 0, (void*)i);
		}
	}
	else
	{
		for(i = 0; i < NR_TST_THREAD; i++)
		{
			_beginthread(mutex_thread, 0, (void*)i);
		}
	}
	
	Start = GetTickCount();
	while(global_value != (MAX_INC_VALUE * NR_TST_THREAD))
	{
		for(i = 0; i < min(NR_TST_THREAD, 4); i++)
		{
			switch(i)
			{
			case 0: p = "a";break;
			case 1: p = "b";break;
			case 2: p = "c";break;
			case 3: p = "d";break;
			}
			fprintf(stdout, "%s %10I64u ", p, thread_value[i]);
		}
		fprintf(stdout,  "* %18I64u\n", global_value);
		Sleep(1000);
	}
	Stop = GetTickCount();
	printf("Time %u milliscond\n", Stop - Start);

	if(f)
		DeleteCriticalSection(&_wincs);
	else
		_mutex.futex = 0;
}