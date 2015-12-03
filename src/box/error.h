#ifndef TARANTOOL_BOX_ERROR_H_INCLUDED
#define TARANTOOL_BOX_ERROR_H_INCLUDED
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
#include "errcode.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** \cond public */

struct error;
/**
 * Error - contains information about error.
 */
typedef struct error box_error_t;

/**
 * Return the error type, e.g. "ClientError", "SocketError", etc.
 * \param error
 * \return not-null string
 */
const char *
box_error_type(const box_error_t *error);

/**
 * Return IPROTO error code
 * \param error error
 * \return enum box_error_code
 */
uint32_t
box_error_code(const box_error_t *error);

/**
 * Return the error message
 * \param error error
 * \return not-null string
 */
const char *
box_error_message(const box_error_t *error);

/**
 * Get the information about the last API call error.
 *
 * The Tarantool error handling works most like libc's errno. All API calls
 * return -1 or NULL in the event of error. An internal pointer to
 * box_error_t type is set by API functions to indicate what went wrong.
 * This value is only significant if API call failed (returned -1 or NULL).
 *
 * Successful function can also touch the last error in some
 * cases. You don't have to clear the last error before calling
 * API functions. The returned object is valid only until next
 * call to **any** API function.
 *
 * You must set the last error using box_error_set() in your stored C
 * procedures if you want to return a custom error message.
 * You can re-throw the last API error to IPROTO client by keeping
 * the current value and returning -1 to Tarantool from your
 * stored procedure.
 *
 * \return last error.
 */
box_error_t *
box_error_last(void);

/**
 * Clear the last error.
 */
void
box_error_clear(void);

/**
 * Set the last error.
 *
 * \param code IPROTO error code (enum \link box_error_code \endlink)
 * \param format (const char * ) - printf()-like format string
 * \param ... - format arguments
 * \returns -1 for convention use
 *
 * \sa enum box_error_code
 */
int
box_error_set(const char *file, unsigned line, uint32_t code,
	      const char *format, ...);

/**
 * A backward-compatible API define.
 */
#define box_error_raise(code, format) \
	box_error_set(__FILE__, __LINE__, code, format)

/** \endcond public */

#if defined(__cplusplus)
} /* extern "C" */
#include "exception.h"

struct rmean;
extern "C" struct rmean *rmean_error;

enum rmean_error_name {
	RMEAN_ERROR,
	RMEAN_ERROR_LAST
};
extern const char *rmean_error_strings[RMEAN_ERROR_LAST];

extern const struct type type_ClientError;
class ClientError: public Exception {
public:
	virtual void raise()
	{
		throw this;
	}

	virtual void log() const;

	int
	errcode() const
	{
		return m_errcode;
	}

	ClientError(const char *file, unsigned line, uint32_t errcode, ...);

	static uint32_t get_errcode(const struct error *e);
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
	template <typename ... Args>
	IllegalParams(const char *file, unsigned line, const char *format,
		      Args ... args)
		:LoggedError(file, line, ER_ILLEGAL_PARAMS,
			     format, args...) {}
};

class ErrorInjection: public LoggedError {
public:
	ErrorInjection(const char *file, unsigned line, const char *msg);
};

void
error_init(void);

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_ERROR_H_INCLUDED */
