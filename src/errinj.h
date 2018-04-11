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
#include <stddef.h>
#include "trivia/util.h"
#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Injection type
 */
enum errinj_type {
	/** boolean */
	ERRINJ_BOOL	= 0,
	/** uint64_t */
	ERRINJ_INT	= 1,
	/** double */
	ERRINJ_DOUBLE   = 2
};

/**
 * Injection state
 */
struct errinj {
	/** Name, e.g "ERRINJ_WAL_WRITE" */
	const char *name;
	/** Type, e.g. BOOL, U64, DOUBLE */
	enum errinj_type type;
	union {
		/** bool parameter */
		bool bparam;
		/** integer parameter */
		int64_t iparam;
		/** double parameter */
		double dparam;
	};
};

/**
 * list of error injection handles.
 */
#define ERRINJ_LIST(_) \
	_(ERRINJ_TESTING, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_WAL_IO, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_WAL_ROTATE, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_WAL_WRITE, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_WAL_WRITE_PARTIAL, ERRINJ_INT, {.iparam = -1}) \
	_(ERRINJ_WAL_WRITE_DISK, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_WAL_WRITE_EOF, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_WAL_DELAY, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_INDEX_ALLOC, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_TUPLE_ALLOC, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_TUPLE_FIELD, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_VY_RUN_WRITE, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_VY_RUN_WRITE_TIMEOUT, ERRINJ_DOUBLE, {.dparam = 0}) \
	_(ERRINJ_VY_RUN_DISCARD, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_VY_INDEX_DUMP, ERRINJ_INT, {.iparam = -1}) \
	_(ERRINJ_VY_TASK_COMPLETE, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_VY_READ_PAGE, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_VY_READ_PAGE_TIMEOUT, ERRINJ_DOUBLE, {.dparam = 0}) \
	_(ERRINJ_VY_SQUASH_TIMEOUT, ERRINJ_DOUBLE, {.dparam = 0}) \
	_(ERRINJ_VY_SCHED_TIMEOUT, ERRINJ_DOUBLE, {.dparam = 0}) \
	_(ERRINJ_VY_GC, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_VY_LOG_FLUSH, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_RELAY_TIMEOUT, ERRINJ_DOUBLE, {.dparam = 0}) \
	_(ERRINJ_RELAY_REPORT_INTERVAL, ERRINJ_DOUBLE, {.dparam = 0}) \
	_(ERRINJ_RELAY_FINAL_SLEEP, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_PORT_DUMP, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_XLOG_GARBAGE, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_XLOG_META, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_XLOG_READ, ERRINJ_INT, {.iparam = -1}) \
	_(ERRINJ_VYRUN_INDEX_GARBAGE, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_VYRUN_DATA_READ, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_BUILD_INDEX, ERRINJ_INT, {.iparam = -1}) \
	_(ERRINJ_VY_POINT_ITER_WAIT, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_RELAY_EXIT_DELAY, ERRINJ_DOUBLE, {.dparam = 0}) \
	_(ERRINJ_VY_DELAY_PK_LOOKUP, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_VY_RUN_WRITE_STMT_TIMEOUT, ERRINJ_DOUBLE, {.dparam = 0}) \
	_(ERRINJ_IPROTO_TX_DELAY, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_HTTPC_EXECUTE, ERRINJ_BOOL, {.bparam = false}) \
	_(ERRINJ_LOG_ROTATE, ERRINJ_BOOL, {.bparam = false}) \

ENUM0(errinj_id, ERRINJ_LIST);
extern struct errinj errinjs[];

/**
 * Returns the error injection by name
 * @param name injection name, e.g ERRINJ_WAL_WRITE
 */
struct errinj *
errinj_by_name(char *name);

typedef int (*errinj_cb)(struct errinj *e, void *cb_ctx);

/**
 * Iterate over all error injections
 */
int
errinj_foreach(errinj_cb cb, void *cb_ctx);

#ifdef NDEBUG
#  define ERROR_INJECT(ID, CODE)
#  define errinj(ID, TYPE) ((struct errinj *) NULL)
#else
#  /* Returns the error injection by id */
#  define errinj(ID, TYPE) \
	({ \
		assert(ID >= 0 && ID < errinj_id_MAX); \
		assert(errinjs[ID].type == TYPE); \
		&errinjs[ID]; \
	})
#  define ERROR_INJECT(ID, CODE) \
	do { \
		if (errinj(ID, ERRINJ_BOOL)->bparam) \
			CODE; \
	} while (0)
#endif

#define ERROR_INJECT_RETURN(ID) ERROR_INJECT(ID, return -1)

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TATRANTOOL_ERRINJ_H_INCLUDED */
