#ifndef INCLUDES_TARANTOOL_LF_LIFO_H
#define INCLUDES_TARANTOOL_LF_LIFO_H
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
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "third_party/pmatomic.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * A very primitive implementation of lock-free
 * LIFO (last in first out, AKA stack, AKA single-linked
 * list with head-only add and remove).
 *
 * It is only usable to store free pages of a memory allocator
 * or similar, since it assumes that all addresses are aligned,
 * and lower 16 bits of address can be used as a counter-based
 * solution for ABA problem.
 */
struct lf_lifo {
	void *next;
};

static inline unsigned short
aba_value(void *a)
{
	return (intptr_t) a & 0xffff;
}

static inline struct lf_lifo *
lf_lifo(void *a)
{
	return (struct lf_lifo *) ((intptr_t) a & ~0xffff);
}

static inline void
lf_lifo_init(struct lf_lifo *head)
{
	head->next = NULL;
}

static inline struct lf_lifo *
lf_lifo_push(struct lf_lifo *head, void *elem)
{
	assert(lf_lifo(elem) == elem); /* Aligned address. */
	do {
		void *tail = head->next;
		lf_lifo(elem)->next = tail;
		/*
		 * Sic: add 1 thus let ABA value overflow, *then*
		 * coerce to unsigned short
		 */
		void *newhead = (char *) elem + aba_value((char *) tail + 1);
		if (pm_atomic_compare_exchange_strong(&head->next, &tail, newhead))
			return head;
	} while (true);
}

static inline void *
lf_lifo_pop(struct lf_lifo *head)
{
	do {
		void *tail = head->next;
		struct lf_lifo *elem = lf_lifo(tail);
		if (elem == NULL)
			return NULL;
		/*
		 * Discard the old tail's aba value, then save
		 * the old head's value in the tail.
		 * This way head's aba value grows monotonically
		 * regardless of the exact sequence of push/pop
		 * operations.
		 */
		void *newhead = ((char *) lf_lifo(elem->next) +
				 aba_value(tail));
		if (pm_atomic_compare_exchange_strong(&head->next, &tail, newhead))
			return elem;
	} while (true);
}

static inline bool
lf_lifo_is_empty(struct lf_lifo *head)
{
	return head->next == NULL;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_LF_LIFO_H */
