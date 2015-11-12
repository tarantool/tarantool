#ifndef TARANTOOL_STAILQ_H_INCLUDED
#define TARANTOOL_STAILQ_H_INCLUDED
/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <stddef.h>
#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#ifndef typeof
/* TODO: 'typeof' is a GNU extension */
#define typeof __typeof__
#endif

struct stailq_entry {
	struct stailq_entry *next;
};

struct stailq {
	struct stailq_entry *first;
	struct stailq_entry **last;
};

/**
 * init list head (or list entry as ins't included in list)
 */

inline static void
stailq_create(struct stailq *head)
{
	head->first = NULL;
	head->last = &head->first;
}

/**
 * Add an item to list head
 */
inline static void
stailq_add(struct stailq *head, struct stailq_entry *item)
{
	item->next = head->first;
	if (item->next == NULL)
		head->last = &item->next;
	head->first = item;
}

/**
 * Pop an item from list head.
 */
inline static struct stailq_entry *
stailq_shift(struct stailq *head)
{
	struct stailq_entry *shift = head->first;
	if ((head->first = head->first->next) == NULL)
		head->last = &head->first;
	return shift;
}

/**
 * Add an item to list tail
 */
inline static void
stailq_add_tail(struct stailq *head, struct stailq_entry *item)
{
	item->next = NULL;
	*head->last = item;
	head->last = &item->next;
}

/**
 * return first element
 */
inline static struct stailq_entry *
stailq_first(struct stailq *head)
{
	return head->first;
}

/**
 * return last element
 */
inline static struct stailq_entry *
stailq_last(struct stailq *head)
{
	return head->last == &head->first ? NULL :
		(struct stailq_entry *) head->last;
}

/**
 * return next element by element
 */
inline static struct stailq_entry *
stailq_next(struct stailq_entry *item)
{
	return item->next;
}

/**
 * return TRUE if list is empty
 */
inline static int
stailq_empty(struct stailq *head)
{
	return head->first == NULL;
}

/*
 * Singly-linked Tail queue functions.
 */
static inline void
stailq_concat(struct stailq *head1, struct stailq *head2)
{
	if (!stailq_empty(head2)) {
		*head1->last = head2->first;
		head1->last = head2->last;
		stailq_create(head2);
	}
}

/* Reverse a list in-place. */
static inline void
stailq_reverse(struct stailq *head)
{
	struct stailq_entry *elem = stailq_first(head), *next;
	stailq_create(head);
	while (elem) {
		next = stailq_next(elem);
		stailq_add(head, elem);
		elem = next;
	}
}

/** Concat all members of head1 starting from elem to the end of head2. */
static inline void
stailq_splice(struct stailq *head1, struct stailq_entry *elem,
	      struct stailq *head2)
{
	if (elem) {
		*head2->last = elem;
		head2->last = head1->last;
		head1->last = &head1->first;
		while (*head1->last != elem)
			head1->last = &(*head1->last)->next;
		*head1->last = NULL;
	}
}

#define stailq_entry(item, type, member) ({				\
	const typeof( ((type *)0)->member ) *__mptr = (item);		\
	(type *)( (char *)__mptr - ((size_t) &((type *)0)->member) ); })

/**
 * return first entry
 */
#define stailq_first_entry(head, type, member)				\
	stailq_entry(stailq_first(head), type, member)

/**
 * return first entry
 */
#define stailq_last_entry(head, type, member)				\
	stailq_entry(stailq_last(head), type, member)

/**
 * return next entry
 */
#define stailq_next_entry(item, member)					\
	stailq_entry(stailq_next(&(item)->member), typeof(*item), member)

#define stailq_foreach_entry(item, head, member)			\
	for (item = stailq_first_entry((head), typeof(*item), member);	\
	     item != stailq_entry(0, typeof(*item), member);		\
	     item = stailq_next_entry(item, member))

#define stailq_foreach_entry_safe(item, next, head, member)		\
	for (item = stailq_first_entry((head), typeof(*item), member);	\
	     item != stailq_entry(0, typeof(*item), member) &&		\
	     (next = stailq_next_entry(item, member), 1);		\
	     item = next)

/**
 * Remove one element from the list and return it
 * @pre the list is not empty
 */
#define stailq_shift_entry(head, type, member)				\
        stailq_entry(stailq_shift(head), type, member)			\

/**
 * add entry to list
 */
#define stailq_add_entry(head, item, member)				\
	stailq_add((head), &(item)->member)

/**
 * add entry to list tail
 */
#define stailq_add_tail_entry(head, item, member)			\
	stailq_add_tail((head), &(item)->member)

/**
 * foreach through list
 */
#define stailq_foreach(item, head)					\
	for (item = stailq_first(head); item; item = item->next)

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_STAILQ_H_INCLUDED */
