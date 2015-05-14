#include "futex/futex.h"
#include <stdio.h>
#include <process.h>
#include <windows.h>

__forceinline
static int atomic_dec(int * minuend)
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
	unsigned int value;
}mutex_t;

//#define	WITH_WIN_CS
#ifdef WITH_WIN_CS
static CRITICAL_SECTION	wincs;
#else
static mutex_t * tst_mutex;
#endif

#define	NR_C	100000000
static unsigned __int64		x0;
static unsigned __int64		x1;
static unsigned __int64		x2;
static unsigned __int64		xx;

static void lock(mutex_t * mutex)
{
	register int		c;
	int					k = 0;
	
	if((c = cmpxchg(&mutex->value, 0, 1)) != 0)
	{
		if(c != 2)
			c = xchg(&mutex->value, 2);
		
		while(c != 0)
		{
			futex(&mutex->value, FUTEX_WAIT, 2, NULL);
			c = xchg(&mutex->value, 2);
//			if(k)
//				printf("Ðé¼Ù»½ÐÑ:%d\n", k);
//			k++;
		}
	}
}

static void unlock(mutex_t * mutex)
{
	if(atomic_dec(&mutex->value) != 1)
	{
		mutex->value = 0;
		futex(&mutex->value, FUTEX_WAKE, 1, NULL);
	}
}

void mutex_thread(void * args)
{
	int	v = (int)args;
	int i;

	for(i = 0; i < NR_C; i++)
	{
#ifdef WITH_WIN_CS
		EnterCriticalSection(&wincs);
#else
		lock(tst_mutex);	
#endif
		switch(v)
		{
		case 0:
			x0++; break;
		case 1:
			x1++; break;
		case 2:
			x2++; break;
		}
		
		xx++;
#ifdef WITH_WIN_CS
		LeaveCriticalSection(&wincs);
#else
		unlock(tst_mutex);
#endif
	}
}

int main()
{
	DWORD	Start;
	DWORD	Stop;

#ifdef WITH_WIN_CS
	InitializeCriticalSection(&wincs);
#else
	tst_mutex = malloc(sizeof(mutex_t));
	if(NULL == tst_mutex)
		return 0;

	tst_mutex->value = 0;
	printf("Hello libfutex 0x%08X\n", tst_mutex);
#endif

	futex(NULL, FUTEX_WAIT, 0, NULL);

	_beginthread(mutex_thread, 0, (void*)0);
	_beginthread(mutex_thread, 0, (void*)1);
	_beginthread(mutex_thread, 0, (void*)2);
	
	Start = GetTickCount();
	while(xx != NR_C * 3)
	{
		printf("a %10I64d b %10I64d c %10I64d x %10I64d\n", x0, x1, x2, xx);
		Sleep(1000);
	}
	Stop = GetTickCount();
	printf("a %10I64d b %10I64d c %10I64d x %10I64d\n", x0, x1, x2, xx);
	printf("Time %d\n", Stop - Start);

	return 0;
}