#ifndef TARANTOOL_LIB_CORE_DIAG_H_INCLUDED
#define TARANTOOL_LIB_CORE_DIAG_H_INCLUDED
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
#include <trivia/util.h>
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

struct type_info;
struct error;

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
	const struct type_info *type;
	/**
	 * Reference counting is basically required since
	 * instances of this structure are available in Lua
	 * as well (as cdata with overloaded fields and methods
	 * by the means of introspection). Thus, it may turn
	 * out that Lua's GC attempts at releasing object
	 * meanwhile it is still used in C internals or vice
	 * versa. For details see luaT_pusherror().
	 */
	int64_t refs;
	/**
	 * Errno at the moment of the error
	 * creation. If the error is not related
	 * to the standard library, then it is 0.
	 */
	int saved_errno;
	/** Line number. */
	unsigned line;
	/* Source file name. */
	char file[DIAG_FILENAME_MAX];
	/* Error description. */
	char errmsg[DIAG_ERRMSG_MAX];
	/**
	 * Link to the cause and effect of given error. The cause
	 * creates the effect:
	 * e1 = box.error.new({code = 0, reason = 'e1'})
	 * e2 = box.error.new({code = 0, reason = 'e2'})
	 * e1:set_prev(e2) -- Now e2 is the cause of e1 and e1 is
	 * the effect of e2.
	 * Only cause keeps reference to avoid cyclic dependence.
	 * RLIST implementation is not really suitable here
	 * since it is organized as circular list. In such
	 * a case it is impossible to start an iteration
	 * from any node and finish at the logical end of the
	 * list. Double-linked list is required to allow deletion
	 * from the middle of the list.
	 */
	struct error *cause;
	struct error *effect;
};

void
error_ref(struct error *e);

void
error_unref(struct error *e);

/**
 * Unlink error from its effect. For instance:
 * e1 -> e2 -> e3 -> e4 (e1:set_prev(e2); e2:set_prev(e3) ...)
 * unlink(e3): e1 -> e2 -> NULL; e3 -> e4 -> NULL
 */
static inline void
error_unlink_effect(struct error *e)
{
	if (e->effect != NULL) {
		assert(e->refs > 1);
		error_unref(e);
		e->effect->cause = NULL;
	}
	e->effect = NULL;
}

/**
 * Set previous error: cut @a prev from its previous 'tail' of
 * causes and link to the one @a e belongs to. Note that all
 * previous errors starting from @a prev->cause are transferred
 * with it as well (i.e. causes for given error are not erased).
 * For instance:
 * e1 -> e2 -> NULL; e3 -> e4 -> NULL;
 * e2:set_effect(e3): e1 -> e2 -> e3 -> e4 -> NULL
 *
 * @a effect can be NULL. To be used as ffi method in
 * lua/error.lua.
 *
 * @retval -1 in case adding @a effect results in list cycles;
 *          0 otherwise.
 */
int
error_set_prev(struct error *e, struct error *prev);

NORETURN static inline void
error_raise(struct error *e)
{
	e->raise(e);
	unreachable();
}

static inline void
error_log(struct error *e)
{
	e->log(e);
}

void
error_create(struct error *e,
	     error_f create, error_f raise, error_f log,
	     const struct type_info *type, const char *file,
	     unsigned line);

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
 * Set a new error to the diagnostics area, replacing existent.
 * \param diag diagnostics area
 * \param e error to add
 */
static inline void
diag_set_error(struct diag *diag, struct error *e)
{
	assert(e != NULL);
	error_ref(e);
	diag_clear(diag);
	error_unlink_effect(e);
	diag->last = e;
}

/**
 * Add a new error to the diagnostics area. It is added to the
 * tail, so that list forms stack.
 * @param diag Diagnostics area.
 * @param e Error to be added.
 */
static inline void
diag_add_error(struct diag *diag, struct error *e)
{
	assert(e != NULL);
	/* diag_set_error() should be called before. */
	assert(diag->last != NULL);
	/*
	 * e should be the bottom of its own stack.
	 * Otherwise some errors may be lost.
	 */
	assert(e->effect == NULL);
	assert(diag->last->effect == NULL);
	error_ref(e);
	e->cause = diag->last;
	diag->last->effect = e;
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

struct diag *
diag_get(void);

NORETURN static inline void
diag_raise(void)
{
	struct error *e = diag_last_error(diag_get());
	assert(e != NULL);
	error_raise(e);
}

static inline void
diag_log(void)
{
	struct error *e = diag_last_error(diag_get());
	assert(e != NULL);
	error_log(e);
}

struct error *
BuildOutOfMemory(const char *file, unsigned line, size_t amount,
		 const char *allocator,
		 const char *object);
struct error *
BuildFiberIsCancelled(const char *file, unsigned line);
struct error *
BuildTimedOut(const char *file, unsigned line);
struct error *
BuildChannelIsClosed(const char *file, unsigned line);
struct error *
BuildLuajitError(const char *file, unsigned line, const char *msg);
struct error *
BuildIllegalParams(const char *file, unsigned line, const char *format, ...);
struct error *
BuildSystemError(const char *file, unsigned line, const char *format, ...);
struct error *
BuildCollationError(const char *file, unsigned line, const char *format, ...);
struct error *
BuildSwimError(const char *file, unsigned line, const char *format, ...);
struct error *
BuildCryptoError(const char *file, unsigned line, const char *format, ...);
struct error *
BuildRaftError(const char *file, unsigned line, const char *format, ...);

struct index_def;

struct error *
BuildUnsupportedIndexFeature(const char *file, unsigned line,
			     struct index_def *index_def, const char *what);
struct error *
BuildSocketError(const char *file, unsigned line, const char *socketname,
		 const char *format, ...);

#define diag_set_detailed(file, line, class, ...) do {			\
	/* Preserve the original errno. */                              \
	int save_errno = errno;                                         \
	say_debug("%s at %s:%i", #class, file, line);			\
	struct error *e;						\
	e = Build##class(file, line, ##__VA_ARGS__);			\
	diag_set_error(diag_get(), e);					\
	/* Restore the errno which might have been reset.  */           \
	errno = save_errno;                                             \
} while (0)

#define diag_set(...)							\
	diag_set_detailed(__FILE__, __LINE__, __VA_ARGS__)

#define diag_add(class, ...) do {					\
	int save_errno = errno;						\
	say_debug("%s at %s:%i", #class, __FILE__, __LINE__);		\
	struct error *e;						\
	e = Build##class(__FILE__, __LINE__, ##__VA_ARGS__);		\
	diag_add_error(diag_get(), e);					\
	errno = save_errno;						\
} while (0)

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_CORE_DIAG_H_INCLUDED */
