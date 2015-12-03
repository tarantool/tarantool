#ifndef TARANTOOL_ERRINJ_H_INCLUDED
#define TARANTOOL_ERRINJ_H_INCLUDED
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
#include "trivia/util.h"
#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct errinj {
	const char *name;
	bool state;
};

/**
 * list of error injection handles.
 */
#define ERRINJ_LIST(_) \
	_(ERRINJ_TESTING, false) \
	_(ERRINJ_WAL_IO, false) \
	_(ERRINJ_WAL_ROTATE, false) \
	_(ERRINJ_WAL_WRITE, false) \
	_(ERRINJ_INDEX_ALLOC, false) \
	_(ERRINJ_TUPLE_ALLOC, false) \
	_(ERRINJ_RELAY, false)

ENUM0(errinj_enum, ERRINJ_LIST);
extern struct errinj errinjs[];

bool errinj_get(int id);

void errinj_set(int id, bool state);
int errinj_set_byname(char *name, bool state);

typedef int (*errinj_cb)(struct errinj *e, void *cb_ctx);
int errinj_foreach(errinj_cb cb, void *cb_ctx);

#ifdef NDEBUG
#  define ERROR_INJECT(ID, CODE)
#else
#  define ERROR_INJECT(ID, CODE) \
	do { \
		if (errinj_get(ID) == true) \
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
