#ifndef TARANTOOL_DIAG_H_INCLUDED
#define TARANTOOL_DIAG_H_INCLUDED
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
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include "say.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum {
	DIAG_ERRMSG_MAX = 512,
	DIAG_FILENAME_MAX = 256
};

struct type;
struct error;
struct error_factory;
extern struct error_factory *error_factory;

typedef void (*error_f)(struct error *e);

/**
 * Error diagnostics needs to be equally usable in C and C++
 * code. This is why there is a common infrastructure for errors.
 *
 * Any error or warning or note is represented by an instance of
 * struct error.
 *
 * struct error has the most common members, but more
 * importantly it has a type descriptor, which makes it possible
 * to work with C++ exceptions and extra members via reflection,
 * in pure C.
 *
 * (destroy) is there to gracefully delete C++ exceptions from C.
 */
struct error {
	error_f destroy;
	error_f raise;
	error_f log;
	const struct type *type;
	int refs;
	/** Line number. */
	unsigned line;
	/* Source file name. */
	char file[DIAG_FILENAME_MAX];
	/* Error description. */
	char errmsg[DIAG_ERRMSG_MAX];
};

static inline void
error_ref(struct error *e)
{
	e->refs++;
}

static inline void
error_unref(struct error *e)
{
	assert(e->refs > 0);
	--e->refs;
	if (e->refs == 0)
		e->destroy(e);
}

static inline void
error_raise(struct error *e)
{
	e->raise(e);
}

static inline void
error_log(struct error *e)
{
	e->log(e);
}

void
error_create(struct error *e,
	     error_f create, error_f raise, error_f log,
	     const struct type *type, const char *file, unsigned line);

void
error_format_msg(struct error *e, const char *format, ...);

void
error_vformat_msg(struct error *e, const char *format, va_list ap);

/**
 * Diagnostics Area - a container for errors
 */
struct diag {
	/* \cond private */
	struct error *last;
	/* \endcond private */
};

/**
 * Create a new diagnostics area
 * \param diag diagnostics area to initialize
 */
static inline void
diag_create(struct diag *diag)
{
	diag->last = NULL;
}
/**
 * Return true if diagnostics area is empty
 * \param diag diagnostics area to initialize
 */
static inline bool
diag_is_empty(struct diag *diag)
{
	return diag->last == NULL;
}

/**
 * Remove all errors from the diagnostics area
 * \param diag diagnostics area
 */
static inline void
diag_clear(struct diag *diag)
{
	if (diag->last == NULL)
		return;
	error_unref(diag->last);
	diag->last = NULL;
}

/**
 * Add a new error to the diagnostics area
 * \param diag diagnostics area
 * \param e error to add
 */
static inline void
diag_add_error(struct diag *diag, struct error *e)
{
	assert(e != NULL);
	error_ref(e);
	diag_clear(diag);
	diag->last = e;
}

/**
 * Move all errors from \a from to \a to.
 * \param from source
 * \param to destination
 * \post diag_is_empty(from)
 */
static inline void
diag_move(struct diag *from, struct diag *to)
{
	diag_clear(to);
	if (from->last == NULL)
		return;
	to->last = from->last;
	from->last = NULL;
}

/**
 * Destroy diagnostics area
 * \param diag diagnostics area to clean
 */
static inline void
diag_destroy(struct diag *diag)
{
	diag_clear(diag);
}

/**
 * Return last error
 * \return last error
 * \param diag diagnostics area
 */
static inline struct error *
diag_last_error(struct diag *diag)
{
	return diag->last;
}

struct error_factory {
	struct error *(*OutOfMemory)(const char *file,
				     unsigned line, size_t amount,
				     const char *allocator,
				     const char *object);
	struct error *(*FiberIsCancelled)(const char *file,
					  unsigned line);
	struct error *(*TimedOut)(const char *file,
				  unsigned line);
	struct error *(*ChannelIsClosed)(const char *file,
					 unsigned line);
	struct error *(*LuajitError)(const char *file,
				     unsigned line, const char *msg);
	struct error *(*ClientError)(const char *file, unsigned line,
				     uint32_t errcode, ...);
};

struct diag *
diag_get();

static inline void
diag_raise(void)
{
	struct error *e = diag_last_error(diag_get());
	if (e)
		error_raise(e);
}


#define diag_set(class, ...) do {					\
	say_debug("%s at %s:%i", #class, __FILE__, __LINE__);		\
	/* No op if exception subsystem is not initialized. */		\
	if (error_factory) {						\
		struct error *e;					\
		e = error_factory->class(__FILE__, __LINE__, ##__VA_ARGS__);\
		diag_add_error(diag_get(), e);				\
	}								\
} while (0)

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_DIAG_H_INCLUDED */
