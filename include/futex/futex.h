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

#define	E_FUTEX_DONE	0	/*	�����ɹ����*/
#define	E_FUTEX_WHOAMI	1	/*	WHOAMIʧ��*/
#define	E_FUTEX_NOMEM	2	/*	�ڴ治��*/
#define	E_FUTEX_PANIC	3	/*	WFSO�쳣����*/	
#define	E_FUTEX_TIMEOUT	4	/*	WFSO�ȴ���ʱ*/
#define	E_FUTEX_FAILED	5	/*	WFSO�ȴ�ʧ��*/
#define	E_FUTEX_EVENT	6	/*	�����¼�ʧ��*/
#define	E_FUTEX_INVAL	7	/*	������������*/
#define	E_FUTEX_ACCESS	8	/*	�ڴ���ʴ���*/

#if defined(LIBFUTEX_EXPORTS)
#	define	LIBFUTEX_API __declspec(dllexport)
#else
#	define	LIBFUTEX_API __declspec(dllimport)
#endif

#define	FUTEX_WAIT		0	/*	futex �ȴ�����*/
#define	FUTEX_WAKE		1	/*	futex ���Ѳ���*/

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
	const struct timespec *tmout);

#ifdef __cplusplus
}
#endif
#endif	/*	__LIBFUTEX_HEADER__*/