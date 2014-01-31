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

#include <stdio.h>
#include <string.h>
#include <errno.h>

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

SystemError::SystemError(const char *file, unsigned line)
	: Exception(file, line),
	  m_errnum(errno)
{
	/* nothing */
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
	_say(S_SYSERROR, m_file, m_line, strerror(m_errnum), "SystemError %s",
	     m_errmsg);
}


ClientError::ClientError(const char *file, unsigned line, uint32_t errcode, ...)
	: Exception(file, line)
{
	m_errcode = errcode;
	va_list ap;
	va_start(ap, errcode);
	vsnprintf(m_errmsg, sizeof(m_errmsg) - 1,
		  tnt_errcode_desc(m_errcode), ap);
	m_errmsg[sizeof(m_errmsg) - 1] = 0;
	va_end(ap);
}

ClientError::ClientError(const char *file, unsigned line, const char *msg,
			 uint32_t errcode)
	: Exception(file, line)
{
	m_errcode = errcode;
	strncpy(m_errmsg, msg, sizeof(m_errmsg) - 1);
	m_errmsg[sizeof(m_errmsg) - 1] = 0;
}

void
ClientError::log() const
{
	_say(S_ERROR, m_file, m_line, NULL, "%s: %s", tnt_errcode_str(m_errcode),
	     m_errmsg);
}

IllegalParams::IllegalParams(const char *file, unsigned line, const char *msg)
	: LoggedError(file, line, ER_ILLEGAL_PARAMS, msg)
{
	/* nothing */
}


ErrorInjection::ErrorInjection(const char *file, unsigned line, const char *msg)
	: LoggedError(file, line, ER_INJECTION, msg)
{
	/* nothing */
}

