#ifndef TARANTOOL_ERRINJ_H_INCLUDED
#define TARANTOOL_ERRINJ_H_INCLUDED
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include <limits.h>
#include "trivia/util.h"
#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum errinj_type {
	ERRINJ_BOOL	= 0,
	ERRINJ_U64	= 1
};

struct errinj {
	const char *name;
	enum errinj_type type;
	union {
		bool bparam;
		uint64_t u64param;
	} state;
};

/**
 * list of error injection handles.
 */
#define ERRINJ_LIST(_) \
	_(ERRINJ_TESTING, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_WAL_IO, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_WAL_ROTATE, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_WAL_WRITE, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_WAL_WRITE_PARTIAL, ERRINJ_U64, {.u64param = UINT64_MAX}) \
	_(ERRINJ_WAL_WRITE_DISK, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_WAL_WRITE_COUNTDOWN, ERRINJ_U64, {.u64param = UINT64_MAX}) \
	_(ERRINJ_WAL_DELAY, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_WAL_SHORT_DELAY, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_INDEX_ALLOC, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_TUPLE_ALLOC, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_TUPLE_FIELD, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_VY_RANGE_DUMP, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_VY_RANGE_SPLIT, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_VY_RUN_DISCARD, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_VY_TASK_COMPLETE, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_VY_READ_PAGE, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_VY_READ_PAGE_TIMEOUT, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_VY_SQUASH_TIMEOUT, ERRINJ_U64, {.u64param = 0}) \
	_(ERRINJ_VY_GC, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_RELAY, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_VINYL_SCHED_TIMEOUT, ERRINJ_U64, {.u64param = 0}) \
	_(ERRINJ_RELAY_FINAL_SLEEP, ERRINJ_BOOL, {.bparam = false})

ENUM0(errinj_enum, ERRINJ_LIST);
extern struct errinj errinjs[];

struct errinj *
errinj_lookup(char *name);

bool errinj_getb(int id);
uint64_t errinj_getu64(int id);

void errinj_setb(int id, bool state);
int errinj_setb_byname(char *name, bool state);
void errinj_setu64(int id, uint64_t state);
int errinj_setu64_byname(char *name, uint64_t state);

typedef int (*errinj_cb)(struct errinj *e, void *cb_ctx);
int errinj_foreach(errinj_cb cb, void *cb_ctx);

#ifdef NDEBUG
#  define ERROR_INJECT(ID, CODE)
#  define ERROR_INJECT_ONCE(ID, CODE)
#  define ERROR_INJECT_U64(ID, EXPR, CODE)
#else
#  define ERROR_INJECT(ID, CODE) \
	do { \
		if (errinj_getb(ID) == true) \
			CODE; \
	} while (0)
#  define ERROR_INJECT_ONCE(ID, CODE) \
	do { \
		if (errinj_getb(ID) == true) { \
			errinj_setb(ID, false); \
			CODE; \
		} \
	} while (0)
#  define ERROR_INJECT_U64(ID, EXPR, CODE) \
	do { \
		if (EXPR) \
			CODE; \
	} while (0)
#endif

#define ERROR_INJECT_EXCEPTION(ID) \
	ERROR_INJECT(ID, tnt_raise(ErrorInjection, #ID))

#define ERROR_INJECT_RETURN(ID) ERROR_INJECT(ID, return -1)

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TATRANTOOL_ERRINJ_H_INCLUDED */
