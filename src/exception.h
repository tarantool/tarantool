#ifndef TARANTOOL_EXCEPTION_H_INCLUDED
#define TARANTOOL_EXCEPTION_H_INCLUDED
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
#include <assert.h>

#include "reflection.h"
#include "say.h"

enum {
	EXCEPTION_ERRMSG_MAX = 512,
	EXCEPTION_FILE_MAX = 256
};

extern const struct type type_Exception;

class Exception {
public:
	const struct type *type; /* reflection */

	void *operator new(size_t size);
	void operator delete(void*);
	virtual void raise()
	{
		/* Throw the most specific type of exception */
		throw this;
	}

	const char *
	errmsg() const
	{
		return m_errmsg;
	}

	const char *file() const {
		return m_file;
	}

	int line() const {
		return m_line;
	}

	virtual void log() const;
	virtual ~Exception();

	void ref() {
		++m_ref;
	}

	void unref() {
		assert(m_ref > 0);
		--m_ref;
		if (m_ref == 0)
			delete this;
	}

	Exception(const Exception &) = delete;
	Exception& operator=(const Exception&) = delete;
protected:
	Exception(const struct type *type, const char *file, unsigned line);

	/* Ref counter */
	size_t m_ref;
	/* line number */
	unsigned m_line;
	/* file name */
	char m_file[EXCEPTION_FILE_MAX];

	/* error description */
	char m_errmsg[EXCEPTION_ERRMSG_MAX];
};

extern const struct type type_SystemError;
class SystemError: public Exception {
public:

	virtual void raise()
	{
		throw this;
	}

	int
	errnum() const
	{
		return m_errno;
	}

	virtual void log() const;

	SystemError(const char *file, unsigned line,
		    const char *format, ...);
protected:
	SystemError(const struct type *type, const char *file, unsigned line);

	void
	init(const char *format, ...);

	void
	init(const char *format, va_list ap);

protected:
	/* system errno */
	int m_errno;
};

extern const struct type type_OutOfMemory;
class OutOfMemory: public SystemError {
public:
	OutOfMemory(const char *file, unsigned line,
		    size_t amount, const char *allocator,
		    const char *object);
};

extern const struct type type_TimedOut;
class TimedOut: public SystemError {
public:
	TimedOut(const char *file, unsigned line);
};

/**
 * Diagnostics Area - a container for errors and warnings
 */
struct diag {
	/* \cond private */
	class Exception *last;
	/* \endcond private */
};

/**
 * Remove all errors from the diagnostics area
 * \param diag diagnostics area
 */
static inline void
diag_clear(struct diag *diag)
{
	if (diag->last == NULL)
		return;
	diag->last->unref();
	diag->last = NULL;
}

/**
 * Add a new error to the diagnostics area
 * \param diag diagnostics area
 * \param e error to add
 */
static inline void
diag_add_error(struct diag *diag, Exception *e)
{
	assert(e != NULL);
	e->ref();
	diag_clear(diag);
	diag->last = e;
}

/**
 * Return last error
 * \return last error
 * \param diag diagnostics area
 */
static inline Exception *
diag_last_error(struct diag *diag)
{
	return diag->last;
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
 * Return true if diagnostics area is empty
 * \param diag diagnostics area to initialize
 */
static inline bool
diag_is_empty(struct diag *diag)
{
	return diag->last == NULL;
}

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
 * Destroy diagnostics area
 * \param diag diagnostics area to clean
 */
static inline void
diag_destroy(struct diag *diag)
{
	diag_clear(diag);
}

/**
 * A helper for tnt_error to avoid cyclic includes (fiber.h and exception.h)
 * \cond false
 * */
struct diag *
diag_get();
/** \endcond */

#define tnt_error(class, ...) ({					\
	say_debug("%s at %s:%i", #class, __FILE__, __LINE__);		\
	class *e = new class(__FILE__, __LINE__, ##__VA_ARGS__);	\
	diag_add_error(diag_get(), e);					\
	e;								\
})

#define tnt_raise(...) do {						\
	throw tnt_error(__VA_ARGS__);					\
} while (0)

#endif /* TARANTOOL_EXCEPTION_H_INCLUDED */
