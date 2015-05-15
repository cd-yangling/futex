# futex
Analog implementation futex system calls on windows.

# 简介
libfutex 是一个模仿 linux NR_futex 系统调用的一个库.
在windows下线程间的同步对象均是分散在实例化的数据上.
考虑下面的一个场景,在文件中有100条记录,每条记录都分别
被临界区保护,那么我需要100个临界区.这意味着我想要100
个Mutex内核对象,如果使用轻量级的CriticalSection,也需要
100个CriticalSection.我们知道CriticalSection在不发生碰撞
的时候并不会产生内核对象.但是一旦产生碰撞系统就会尝试
产生一个内核对象.即使你尝试在CriticalSection设置spin.
最坏的情况下依然可能产生100个内核对象.libfutex从线程的
行为角度出发.其实无论你有多少条记录需要保护,作为线程本
身当时的情况只可能等待或者运行.也就是说同步线程的行为最多
只需要与线程数量相等的内核对象.

# 实现
libfutex 使用 Windows Tls家族函数 为每个线程关联类似内核
线程描述符 task_t 在这个task_t 上分配一个自动事件内核对象
当线程需要执行等待的时候 调用 TlsGetValue 获得自己的task_t
把自己挂入等待队列.然后执行 WaitForSignalObject.几乎达到
类似linux NR_futex system call 的行为.

# 扩展
一旦 libfutex 的行为和 linux 内核的 NR_futex system call一致
那么最开始我们考虑的场景中100个记录无需要再用100个内核对象.
即使再多的记录,始终最后内核对象的数量只会和线程数量一致.这有利于
节约大量的内核对象用于程序的其它地方.这对于大型程序我认为很有用.

一旦libfutex可行.后续我将考虑移植 glibc 中的 nptl 模块,实现一个
(Lightweight Posix Thread For Windows) lwptw 其实在windows下
使用pthread已经有成熟的cygwin pthread for win32等开源库.但是他们
都有一个问题就是把内核对象分散在每个被保护的对象上.这对于我当前
所在公司维护的程序需要大量使用内核对象的句柄,简直是一个灾难.

基于这个理由我才考虑要实现一个类似 NR_futex system call 的库
其实最好的方式应该是以windows驱动的模块进行开发.直接放入windows
内核.但是我自己对windows驱动的开发,并不是很熟悉.所以就放弃了.

