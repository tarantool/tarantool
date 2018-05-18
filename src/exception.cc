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
#include "exception.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "fiber.h"
#include "reflection.h"

extern "C" {

static void
exception_destroy(struct error *e)
{
	delete (Exception *) e;
}

static void
exception_raise(struct error *error)
{
	Exception *e = (Exception *) error;
	e->raise();
}

static void
exception_log(struct error *error)
{
	Exception *e = (Exception *) error;
	e->log();
}

const char *
exception_get_string(struct error *e, const struct method_info *method)
{
	/* A workaround for for vtable */
	Exception *ex = (Exception *) e;
	if (!method_invokable<const char *>(method, ex))
		return NULL;
	return method_invoke<const char *>(method, ex);
}

int
exception_get_int(struct error *e, const struct method_info *method)
{
	/* A workaround for vtable  */
	Exception *ex = (Exception *) e;
	if (!method_invokable<int>(method, ex))
		return 0;
	return method_invoke<int>(method, ex);
}

} /* extern "C" */

/** out_of_memory::size is zero-initialized by the linker. */
static OutOfMemory out_of_memory(__FILE__, __LINE__,
				 sizeof(OutOfMemory), "malloc", "exception");

static const struct method_info exception_methods[] = {
	make_method(&type_Exception, "message", &Exception::get_errmsg),
	make_method(&type_Exception, "log", &Exception::log),
	METHODS_SENTINEL
};
const struct type_info type_Exception = make_type("Exception", NULL,
	exception_methods);

void *
Exception::operator new(size_t size)
{
	void *buf = malloc(size);
	if (buf != NULL)
		return buf;
	diag_add_error(diag_get(), &out_of_memory);
	throw &out_of_memory;
}

void
Exception::operator delete(void *ptr)
{
	free(ptr);
}

Exception::~Exception()
{
	if (this != &out_of_memory) {
		assert(refs == 0);
	}
}

Exception::Exception(const struct type_info *type_arg, const char *file,
		     unsigned line)
{
	error_create(this, exception_destroy, exception_raise,
		     exception_log, type_arg, file, line);
}

void
Exception::log() const
{
	say_file_line(S_ERROR, file, line, errmsg, "%s", type->name);
}

static const struct method_info systemerror_methods[] = {
	make_method(&type_SystemError, "errno", &SystemError::get_errno),
	METHODS_SENTINEL
};

const struct type_info type_SystemError =
	make_type("SystemError", &type_Exception, systemerror_methods);

SystemError::SystemError(const struct type_info *type,
			 const char *file, unsigned line)
	:Exception(type, file, line),
	m_errno(errno)
{
	/* nothing */
}

SystemError::SystemError(const char *file, unsigned line,
			 const char *format, ...)
	: Exception(&type_SystemError, file, line),
	m_errno(errno)
{
	va_list ap;
	va_start(ap, format);
	error_vformat_msg(this, format, ap);
	va_end(ap);
}

void
SystemError::log() const
{
	say_file_line(S_SYSERROR, file, line, strerror(m_errno),
		      "SystemError %s", errmsg);
}

const struct type_info type_OutOfMemory =
	make_type("OutOfMemory", &type_SystemError);

OutOfMemory::OutOfMemory(const char *file, unsigned line,
			 size_t amount, const char *allocator,
			 const char *object)
	: SystemError(&type_OutOfMemory, file, line)
{
	m_errno = ENOMEM;
	error_format_msg(this, "Failed to allocate %u bytes in %s for %s",
			 (unsigned) amount, allocator, object);
}

const struct type_info type_TimedOut =
	make_type("TimedOut", &type_SystemError);

TimedOut::TimedOut(const char *file, unsigned line)
	: SystemError(&type_TimedOut, file, line)
{
	m_errno = ETIMEDOUT;
	error_format_msg(this, "timed out");
}

const struct type_info type_ChannelIsClosed =
	make_type("ChannelIsClosed", &type_Exception);

ChannelIsClosed::ChannelIsClosed(const char *file, unsigned line)
	: Exception(&type_ChannelIsClosed, file, line)
{
	error_format_msg(this, "channel is closed");
}

const struct type_info type_FiberIsCancelled =
	make_type("FiberIsCancelled", &type_Exception);

FiberIsCancelled::FiberIsCancelled(const char *file, unsigned line)
	: Exception(&type_FiberIsCancelled, file, line)
{
	error_format_msg(this, "fiber is cancelled");
}

void
FiberIsCancelled::log() const
{
	say_info("fiber `%s' has been cancelled",
		 fiber_name(fiber()));
	say_info("fiber `%s': exiting", fiber_name(fiber()));
}

const struct type_info type_LuajitError =
	make_type("LuajitError", &type_Exception);

LuajitError::LuajitError(const char *file, unsigned line,
			 const char *msg)
	: Exception(&type_LuajitError, file, line)
{
	snprintf(errmsg, sizeof(errmsg), "%s", msg ? msg : "");
}

const struct type_info type_IllegalParams =
	make_type("IllegalParams", &type_Exception);

IllegalParams::IllegalParams(const char *file, unsigned line,
				     const char *format, ...)
	: Exception(&type_IllegalParams, file, line)
{
	va_list ap;
	va_start(ap, format);
	error_vformat_msg(this, format, ap);
	va_end(ap);
}

const struct type_info type_CollationError =
	make_type("CollationError", &type_Exception);

CollationError::CollationError(const char *file, unsigned line,
			       const char *format, ...)
	: Exception(&type_CollationError, file, line)
{
	va_list ap;
	va_start(ap, format);
	error_vformat_msg(this, format, ap);
	va_end(ap);
}

#define BuildAlloc(type)				\
	void *p = malloc(sizeof(type));			\
	if (p == NULL)					\
		return &out_of_memory;

struct error *
BuildOutOfMemory(const char *file, unsigned line,
		 size_t amount, const char *allocator,
		 const char *object)
{
	BuildAlloc(OutOfMemory);
	return new (p) OutOfMemory(file, line, amount, allocator,
				   object);
}

struct error *
BuildTimedOut(const char *file, unsigned line)
{
	BuildAlloc(TimedOut);
	return new (p) TimedOut(file, line);
}

struct error *
BuildChannelIsClosed(const char *file, unsigned line)
{
	BuildAlloc(ChannelIsClosed);
	return new (p) ChannelIsClosed(file, line);
}

struct error *
BuildFiberIsCancelled(const char *file, unsigned line)
{
	BuildAlloc(FiberIsCancelled);
	return new (p) FiberIsCancelled(file, line);
}

struct error *
BuildLuajitError(const char *file, unsigned line, const char *msg)
{
	BuildAlloc(LuajitError);
	return new (p) LuajitError(file, line, msg);
}

struct error *
BuildIllegalParams(const char *file, unsigned line, const char *format, ...)
{
	BuildAlloc(IllegalParams);
	IllegalParams *e =  new (p) IllegalParams(file, line, "");
	va_list ap;
	va_start(ap, format);
	error_vformat_msg(e, format, ap);
	va_end(ap);
	return e;
}

struct error *
BuildSystemError(const char *file, unsigned line, const char *format, ...)
{
	BuildAlloc(SystemError);
	SystemError *e = new (p) SystemError(file, line, "");
	va_list ap;
	va_start(ap, format);
	error_vformat_msg(e, format, ap);
	va_end(ap);
	return e;
}

struct error *
BuildCollationError(const char *file, unsigned line, const char *format, ...)
{
	BuildAlloc(CollationError);
	CollationError *e =  new (p) CollationError(file, line, "");
	va_list ap;
	va_start(ap, format);
	error_vformat_msg(e, format, ap);
	va_end(ap);
	return e;
}

void
exception_init()
{
	/* A special workaround for out_of_memory static init */
	out_of_memory.refs = 1;
}

#undef BuildAlloc
