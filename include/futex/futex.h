/**
 *	futex/futex.h
 *
 *	Copyright (C) 2014 YangLing(yl.tienon@gmail.com)
 *
 *	Description:
 *
 *	Revision History:
 *
 *	2015-05-12 Created By YangLing
 */

#ifndef	__LIBFUTEX_HEADER__
#define	__LIBFUTEX_HEADER__

#define	E_FUTEX_DONE	0	/*	操作成功完成*/
#define	E_FUTEX_WHOAMI	1	/*	WHOAMI失败*/
#define	E_FUTEX_NOMEM	2	/*	内存不足*/
#define	E_FUTEX_PANIC	3	/*	WFSO异常返回*/	
#define	E_FUTEX_TIMEOUT	4	/*	WFSO等待超时*/
#define	E_FUTEX_FAILED	5	/*	WFSO等待失败*/
#define	E_FUTEX_EVENT	6	/*	创建事件失败*/
#define	E_FUTEX_INVAL	7	/*	操作参数错误*/
#define	E_FUTEX_ACCESS	8	/*	内存访问错误*/

#if defined(LIBFUTEX_EXPORTS)
#	define	LIBFUTEX_API __declspec(dllexport)
#else
#	define	LIBFUTEX_API __declspec(dllimport)
#endif

#define	FUTEX_WAIT		0	/*	futex 等待操作*/
#define	FUTEX_WAKE		1	/*	futex 唤醒操作*/

#ifndef HAVE_STRUCT_TIMESPEC
#define HAVE_STRUCT_TIMESPEC 1
/**
 *	POSIX.1b structure for a time value.
 *	This is like a `struct timeval' but
 *	has nanoseconds instead of microseconds.
 */
struct timespec {
	long tv_sec;			/* Seconds.  */
	long tv_nsec;			/* Nanoseconds.  */
};
#endif /* HAVE_STRUCT_TIMESPEC */

#ifdef	__cplusplus
extern	"C" {
#endif

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
	const struct timespec *tmout);

#ifdef __cplusplus
}
#endif
#endif	/*	__LIBFUTEX_HEADER__*/