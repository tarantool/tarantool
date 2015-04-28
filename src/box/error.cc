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
#include "error.h"
#include <stdio.h>
#include <typeinfo>

ClientError::ClientError(const char *file, unsigned line,
			 uint32_t errcode, ...)
	: Exception(file, line)
{
	m_errcode = errcode;
	va_list ap;
	va_start(ap, errcode);
	vsnprintf(m_errmsg, sizeof(m_errmsg),
		  tnt_errcode_desc(m_errcode), ap);
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
	_say(S_ERROR, m_file, m_line, m_errmsg, "%s", tnt_errcode_str(m_errcode));
}


uint32_t
ClientError::get_errcode(const Exception *e)
{
	const ClientError *error = dynamic_cast<const ClientError *>(e);
	if (error)
		return error->errcode();
	if (typeid(*e) == typeid(OutOfMemory))
		return ER_MEMORY_ISSUE;
	return ER_PROC_LUA;
}

ErrorInjection::ErrorInjection(const char *file, unsigned line, const char *msg)
	: LoggedError(file, line, ER_INJECTION, msg)
{
	/* nothing */
}

