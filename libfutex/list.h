/**
 *	以下代码从
 *	
 *	http://www.cs.fsu.edu/~baker/devices/lxr/http/source/linux/include/linux/list.h
 *
 *	复制得到,同时简单做了一些修改
 */

#ifndef __LIBFUTEX_LIST_H__
#define __LIBFUTEX_LIST_H__

#ifndef	always_inline
#define	always_inline	__forceinline
#endif

struct list_head {
	struct list_head *next, *prev;
};

/*
 * Simple doubly linked list implementation.
 *
 * Some of the internal functions ("__xxx") are useful when
 * manipulating whole lists rather than single entries, as
 * sometimes we already know the next/prev entries and we can
 * generate better code by using them directly rather than
 * using the generic single-entry routines.
 */
#define INIT_LIST_HEAD(ptr) do { \
	(ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

/*
 * Insert a new1 entry between two known consecutive entries. 
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
always_inline static
void __list_add(
	struct list_head * new1,
	struct list_head * prev,
	struct list_head * next)
{
	next->prev = new1;
	new1->next = next;
	new1->prev = prev;
	prev->next = new1;
}

/**
 * list_add - add a new entry
 * @new: new entry to be added
 * @head: list head to add it after
 *
 * Insert a new1 entry after the specified head.
 * This is good for implementing stacks.
 */
always_inline static
void list_add(
	struct list_head *new1,
	struct list_head *head)
{
	__list_add(new1, head, head->next);
}

/**
 * list_add_tail - add a new1 entry
 * @new1: new1 entry to be added
 * @head: list head to add it before
 *
 * Insert a new1 entry before the specified head.
 * This is useful for implementing queues.
 */
always_inline static
void list_add_tail(
	struct list_head *new1,
	struct list_head *head)
{
	__list_add(new1, head->prev, head);
}

/*
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
always_inline static
void __list_del(
	struct list_head * prev,
	struct list_head * next)
{
	next->prev = prev;
	prev->next = next;
}

/**
 * list_del_init - deletes entry from list and reinitialize it.
 * @entry: the element to delete from the list.
 */
always_inline static
void list_del_init(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
	INIT_LIST_HEAD(entry); 
}

/**
 * list_empty - tests whether a list is empty
 * @head: the list to test.
 */
always_inline static
int list_empty(struct list_head *head)
{
	return head->next == head;
}

/**
 * list_entry - get the struct for this entry
 * @ptr:	the &struct list_head pointer.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the list_struct within the struct.
 */
#define list_entry(ptr, type, member) \
	((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

/**
 * list_for_each	-	iterate over a list
 * @pos:	the &struct list_head to use as a loop counter.
 * @head:	the head for your list.
 */
#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); \
        	pos = pos->next)
        	
/**
 * list_for_each_safe	-	iterate over a list safe against removal of list entry
 * @pos:	the &struct list_head to use as a loop counter.
 * @n:		another &struct list_head to use as temporary storage
 * @head:	the head for your list.
 */
#define list_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
		pos = n, n = pos->next)

#endif	/*	__LIBFUTEX_LIST_H__*/
