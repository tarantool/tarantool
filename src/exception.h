#ifndef TARANTOOL_EXCEPTION_H_INCLUDED
#define TARANTOOL_EXCEPTION_H_INCLUDED
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
#include <assert.h>

#include "reflection.h"
#include "diag.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

extern const struct type_info type_Exception;
extern const struct type_info type_OutOfMemory;
extern const struct type_info type_FiberIsCancelled;
extern const struct type_info type_TimedOut;
extern const struct type_info type_ChannelIsClosed;
extern const struct type_info type_LuajitError;
extern const struct type_info type_IllegalParams;
extern const struct type_info type_SystemError;
extern const struct type_info type_CollationError;

const char *
exception_get_string(struct error *e, const struct method_info *method);

int
exception_get_int(struct error *e, const struct method_info *method);

#if defined(__cplusplus)
} /* extern "C" */

class Exception: public error {
public:
	void *operator new(size_t size);
	void *operator new(size_t size, void *p) { (void) size; return p; }
	void operator delete(void*);

	const char *get_file() const { return file; }
	int get_line() const { return line; }
	const char *get_errmsg() const { return errmsg; }

	NORETURN virtual void raise() = 0;
	virtual void log() const;
	virtual ~Exception();

	Exception(const Exception &) = delete;
	Exception& operator=(const Exception&) = delete;
protected:
	Exception(const struct type_info *type, const char *file, unsigned line);
};

class SystemError: public Exception {
public:
	virtual void raise() { throw this; }

	int get_errno() const { return m_errno; }

	virtual void log() const;

	SystemError(const char *file, unsigned line,
		    const char *format, ...);
protected:
	SystemError(const struct type_info *type, const char *file, unsigned line);
protected:
	/* system errno */
	int m_errno;
};

class OutOfMemory: public SystemError {
public:
	OutOfMemory(const char *file, unsigned line,
		    size_t amount, const char *allocator,
		    const char *object);
	virtual void raise() { throw this; }
};

class TimedOut: public SystemError {
public:
	TimedOut(const char *file, unsigned line);
	virtual void raise() { throw this; }
};

class ChannelIsClosed: public Exception {
public:
	ChannelIsClosed(const char *file, unsigned line);
	virtual void raise() { throw this; }
};

/**
 * This is thrown by fiber_* API calls when the fiber is
 * cancelled.
 */
class FiberIsCancelled: public Exception {
public:
	FiberIsCancelled(const char *file, unsigned line);
	virtual void log() const;
	virtual void raise() { throw this; }
};

class LuajitError: public Exception {
public:
	LuajitError(const char *file, unsigned line,
		    const char *msg);
	virtual void raise() { throw this; }
};

class IllegalParams: public Exception {
public:
	IllegalParams(const char *file, unsigned line, const char *format, ...);
	virtual void raise() { throw this; }
};

class CollationError: public Exception {
public:
	CollationError(const char *file, unsigned line, const char *format,
		       ...);
	virtual void raise() { throw this; }
};

/**
 * Initialize the exception subsystem.
 */
void
exception_init();

#define tnt_error(class, ...) ({					\
	say_debug("%s at %s:%i", #class, __FILE__, __LINE__);		\
	class *e = new class(__FILE__, __LINE__, ##__VA_ARGS__);	\
	diag_add_error(diag_get(), e);					\
	e;								\
})

#define tnt_raise(...) do {						\
	throw tnt_error(__VA_ARGS__);					\
} while (0)

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_EXCEPTION_H_INCLUDED */
