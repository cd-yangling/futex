/**
 *	spinlock.h
 *
 *	Copyright (C) 2014 YangLing(yl.tienon@gmail.com)
 *
 *	Description:
 *
 *	Revision History:
 *
 *	2015-05-12 Created By YangLing
 */

#ifndef	__LIBFUTEX_SPINLOCK_H__
#define	__LIBFUTEX_SPINLOCK_H__

/**
 *	href: http://x86.renejeschke.de/html/file_module_x86_id_327.html (XADD  instruction)
 *	href: http://x86.renejeschke.de/html/file_module_x86_id_232.html (PAUSE instruction)
 *	href: https://msdn.microsoft.com/en-us/library/4ks26t93.aspx (Inline Assembler For MSVS)
 *	href: http://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-instruction-set-reference-manual-325383.pdf
 *
 *	WARNING:
 *	PAUSE 指令 需要  Pentium 4 or Intel Xeon processor 但是向前兼容 PAUSE OPCODE (0xF3 0x90) 我未测试
 *
 *	0xF3 是 rep 的前缀
 *	0x90 是 nop 的操作码
 *
 *	那么在早期的CPU,应该是nop循环. 这点需要确认
 *	
 *	XADD  指令 需要  至少 486 的CPU
 *
 *	这里实现的是一个 FIFO 的自旋锁,原理参考下面链接
 *	href: http://www.ibm.com/developerworks/cn/linux/l-cn-spinlock
 *	href: http://lwn.net/Articles/267968/
 */

#ifndef	always_inline
#define	always_inline	__forceinline
#endif

typedef struct spinlock_s {
	volatile unsigned short enter;
	volatile unsigned short leave;
} spinlock_t;

static void spin_init(spinlock_t * slock)
{
	*(unsigned int *)slock = 0;
}

static void spin_destroy(spinlock_t * slock)
{
	*(unsigned int *)slock = 0;
}

always_inline static
void spin_release(spinlock_t * slock)
{
	__asm
	{
		/*	let edx = slock*/
		mov			edx, dword ptr [slock];

		/*	slock->leave++*/
		/*	hardware memory barrier. avoid CPU out-of-order*/
		lock inc	word ptr [edx + 2];
	}
}

always_inline static
void spin_acquire(spinlock_t * slock)
{
	/**
	 *	汇编代码块的C逻辑表达
	 *
	 *	int enter; 其中STEP1 和 STEP2 是一个原子的操作过程
	 *
	 *	enter = slock->enter;	STEP1
	 *	slock->enter += 1;		STEP2
	 *	while(enter != slock->leave)
	 *	{
	 *		_asm pause;
	 *	}
	 */
	__asm
	{
		/*	let eax = 1*/
		mov			eax, 1;

		/*	let edx = slock*/
		mov			edx, dword ptr [slock];
		/**
		 *	word ptr [edx] == slock->enter;
		 *
		 *	atomic operate behavior
		 *
		 *	tmp = slock->enter;
		 *	slock->enter += ax;
		 *	ax = tmp;
		 */
		/*	hardware memory barrier. avoid CPU out-order*/
		lock xadd	word ptr [edx], ax;

spin_wait_loops:

		/*	let bx = slock->leave*/
		mov			bx, word ptr [edx + 2];

		/*	compare ax with slock->leave*/
		cmp			ax, bx;

		/*	if not equal goto retry. Otherwise ByeBye */
		je			hold_spin_lock;

		/**
		 *	pause instruction
		 *	2 个目的
		 *	1: 减少电能消耗
		 *	2: uses this hint to avoid the memory order violation in most situations
		 */
		rep nop;

		/*	unconditionally jmp to spin_wait_loops*/
		jmp			spin_wait_loops;
hold_spin_lock:
	}
}

#endif	/*	__LIBFUTEX_SPINLOCK_H__*/
