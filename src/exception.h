#ifndef TARANTOOL_EXCEPTION_H_INCLUDED
#define TARANTOOL_EXCEPTION_H_INCLUDED
/*
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
#include "object.h"
#include <stdarg.h>
#include "errcode.h"
#include "say.h"

class Exception: public Object {
public:
	void *operator new(size_t size);
	void operator delete(void*);

	const char *
	errmsg() const
	{
		return m_errmsg;
	}

	virtual void log() const = 0;

protected:
	Exception(const char *file, unsigned line);
	/* The copy constructor is needed for C++ throw */
	Exception(const Exception&);

	/* file name */
	const char *m_file;
	/* line number */
	unsigned m_line;

	/* error description */
	char m_errmsg[TNT_ERRMSG_MAX];
};

class SystemError: public Exception {
public:

	int
	errnum() const
	{
		return m_errnum;
	}

	virtual void log() const;

protected:
	SystemError(const char *file, unsigned line);

	void
	init(const char *format, ...);

	void
	init(const char *format, va_list ap);

private:
	/* system errno */
	int m_errnum;
};

class ClientError: public Exception {
public:
	virtual void log() const;

	int
	errcode() const
	{
		return m_errcode;
	}

	ClientError(const char *file, unsigned line, uint32_t errcode, ...);
	/* A special constructor for lbox_raise */
	ClientError(const char *file, unsigned line, const char *msg,
		    uint32_t errcode);
private:
	/* client errno code */
	int m_errcode;
};

class LoggedError: public ClientError {
public:
	template <typename ... Args>
	LoggedError(const char *file, unsigned line, uint32_t errcode, Args ... args)
		: ClientError(file, line, errcode, args...)
	{
		/* TODO: actually calls ClientError::log */
		log();
	}
};

class IllegalParams: public LoggedError {
public:
	IllegalParams(const char *file, unsigned line, const char *msg);
};

class ErrorInjection: public LoggedError {
public:
	ErrorInjection(const char *file, unsigned line, const char *msg);
};

#define tnt_raise(...) tnt_raise0(__VA_ARGS__)
#define tnt_raise0(class, ...) do {					\
	say_debug("%s at %s:%i", #class, __FILE__, __LINE__);		\
	throw new class(__FILE__, __LINE__, ##__VA_ARGS__);		\
} while (0)

#endif /* TARANTOOL_EXCEPTION_H_INCLUDED */
