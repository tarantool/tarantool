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
#include "error.h"
#include <stdio.h>

#include <fiber.h>
struct rmean *rmean_error = NULL;

const char *rmean_error_strings[RMEAN_ERROR_LAST] = {
	"ERROR"
};

static struct method clienterror_methods[] = {
	make_method(&type_ClientError, "code", &ClientError::errcode),
	METHODS_SENTINEL
};
const struct type type_ClientError = make_type("ClientError", &type_Exception,
	clienterror_methods);

ClientError::ClientError(const char *file, unsigned line,
			 uint32_t errcode, ...)
	: Exception(&type_ClientError, file, line)
{
	m_errcode = errcode;
	va_list ap;
	va_start(ap, errcode);
	vsnprintf(m_errmsg, sizeof(m_errmsg),
		  tnt_errcode_desc(m_errcode), ap);
	if (rmean_error)
		rmean_collect(rmean_error, RMEAN_ERROR, 1);
	va_end(ap);
}

ClientError::ClientError(const char *file, unsigned line, const char *msg,
			 uint32_t errcode)
	: Exception(&type_ClientError, file, line)
{
	m_errcode = errcode;
	strncpy(m_errmsg, msg, sizeof(m_errmsg) - 1);
	m_errmsg[sizeof(m_errmsg) - 1] = 0;
	if (rmean_error)
		rmean_collect(rmean_error, RMEAN_ERROR, 1);
}

void
ClientError::log() const
{
	_say(S_ERROR, m_file, m_line, m_errmsg, "%s", tnt_errcode_str(m_errcode));
}


uint32_t
ClientError::get_errcode(const Exception *e)
{
	ClientError *client_error = type_cast(ClientError, e);
	if (client_error)
		return client_error->errcode();
	if (type_cast(OutOfMemory, e))
		return ER_MEMORY_ISSUE;
	return ER_PROC_LUA;
}

ErrorInjection::ErrorInjection(const char *file, unsigned line, const char *msg)
	: LoggedError(file, line, ER_INJECTION, msg)
{
	/* nothing */
}

const char *
box_error_type(const box_error_t *error)
{
	Exception *e = (Exception *) error;
	return e->type->name;
}

uint32_t
box_error_code(const box_error_t *error)
{
	Exception *e = (Exception *) error;
	return ClientError::get_errcode(e);
}

const char *
box_error_message(const box_error_t *error)
{
	Exception *e = (Exception *) error;
	return e->errmsg();
}

const box_error_t *
box_error_last(void)
{
	return (box_error_t *) diag_last_error(&fiber()->diag);
}

void
box_error_clear(void)
{
	diag_clear(&fiber()->diag);
}

int
box_error_raise(uint32_t code, const char *fmt, ...)
{
	char msg[DIAG_ERRMSG_MAX];

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	tnt_error(ClientError, msg, code);
	return -1;
}
