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
#include "exception.h"
#include "say.h"
#include "fiber.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <typeinfo>

static OutOfMemory out_of_memory(__FILE__, __LINE__,
				 sizeof(OutOfMemory), "malloc", "exception");

void *
Exception::operator new(size_t size)
{
	struct cord *cord = cord();

	if (cord->exception == &out_of_memory) {
		assert(cord->exception_size == 0);
		cord->exception = NULL;
	}
	if (cord->exception) {
		/* Explicitly call destructor for previous exception */
		cord->exception->~Exception();
		if (cord->exception_size >= size) {
			/* Reuse memory allocated for exception */
			return cord->exception;
		}
		free(cord->exception);
	}
	cord->exception = (Exception *) malloc(size);
	if (cord->exception) {
		cord->exception_size = size;
		return cord->exception;
	}
	cord->exception = &out_of_memory;
	cord->exception_size = 0;
	throw cord->exception;
}

void
Exception::init(struct cord *cord)
{
	cord->exception = NULL;
	cord->exception_size = 0;
}

void
Exception::cleanup(struct cord *cord)
{
	if (cord->exception != NULL && cord->exception != &out_of_memory) {
		cord->exception->~Exception();
		free(cord->exception);
	}
	Exception::init(cord);
}

void
Exception::move(struct cord *from, struct cord *to)
{
	Exception::cleanup(to);
	to->exception = from->exception;
	to->exception_size = from->exception_size;
	Exception::init(from);
}


void
Exception::operator delete(void * /* ptr */)
{
	/* Unsupported */
	assert(false);
}

Exception::Exception(const char *file, unsigned line)
	: m_file(file), m_line(line)
{
	m_errmsg[0] = 0;
}

Exception::Exception(const Exception& e)
	: Object(), m_file(e.m_file), m_line(e.m_line)
{
	memcpy(m_errmsg, e.m_errmsg, sizeof(m_errmsg));
}

void
Exception::log() const
{
	_say(S_ERROR, m_file, m_line, "%s %s",
	     typeid(*this).name(), m_errmsg);
}


SystemError::SystemError(const char *file, unsigned line)
	: Exception(file, line),
	  m_errno(errno)
{
	/* nothing */
}

SystemError::SystemError(const char *file, unsigned line,
			 const char *format, ...)
	:Exception(file, line),
	m_errno(errno)
{
	va_list ap;
	va_start(ap, format);
	init(format, ap);
	va_end(ap);
}

void
SystemError::init(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	init(format, ap);
	va_end(ap);
}

void
SystemError::init(const char *format, va_list ap)
{
	vsnprintf(m_errmsg, sizeof(m_errmsg), format, ap);
}

void
SystemError::log() const
{
	_say(S_SYSERROR, m_file, m_line, strerror(m_errno), "SystemError %s",
	     m_errmsg);
}

OutOfMemory::OutOfMemory(const char *file, unsigned line,
			 size_t amount, const char *allocator,
			 const char *object)
	:SystemError(file, line)
{
	m_errno = ENOMEM;
	snprintf(m_errmsg, sizeof(m_errmsg),
		 "Failed to allocate %u bytes in %s for %s",
		 (unsigned) amount, allocator, object);
}
