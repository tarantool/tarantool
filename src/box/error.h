#ifndef TARANTOOL_BOX_ERROR_H_INCLUDED
#define TARANTOOL_BOX_ERROR_H_INCLUDED
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
#include "errcode.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct vclock;

struct error *
BuildClientError(const char *file, unsigned line, uint32_t errcode, ...);

struct error *
BuildAccessDeniedError(const char *file, unsigned int line,
		       const char *access_type, const char *object_type,
		       const char *object_name, const char *user_name);

struct error *
BuildXlogError(const char *file, unsigned line, const char *format, ...);

struct error *
BuildXlogGapError(const char *file, unsigned line,
		  const struct vclock *from, const struct vclock *to);

struct error *
BuildCustomError(const char *file, unsigned int line, const char *custom_type,
		 uint32_t errcode);

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
#define box_error_raise(code, format, ...) \
	box_error_set(__FILE__, __LINE__, code, format, ##__VA_ARGS__)

/** \endcond public */

/**
 * Return the error custom type. It is NULL in case the error
 * does not have it.
 * @param e Error object.
 * @return Pointer to custom error type.
 */
const char *
box_error_custom_type(const struct error *e);

/**
 * Add error to the diagnostic area. In contrast to box_error_set()
 * it does not replace previous error being set, but rather link
 * them into list.
 *
 * \param code IPROTO error code (enum \link box_error_code \endlink)
 * \param custom_type User-defined error type which will be
 *       displayed instead of ClientError.
 * \param format (const char * ) - printf()-like format string
 * \param ... - format arguments
 * \returns -1 for convention use
 *
 * \sa enum box_error_code
 */
int
box_error_add(const char *file, unsigned line, uint32_t code,
	      const char *custom_type, const char *fmt, ...);

/**
 * Construct error object without setting it in the diagnostics
 * area. On the memory allocation fail returns OutOfMemory error.
 */
struct error *
box_error_new(const char *file, unsigned line, uint32_t code,
	      const char *custom_type, const char *fmt, ...);

extern const struct type_info type_ClientError;
extern const struct type_info type_XlogError;
extern const struct type_info type_XlogGapError;
extern const struct type_info type_AccessDeniedError;
extern const struct type_info type_CustomError;

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

class ClientError: public Exception
{
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
protected:
	ClientError(const type_info *type, const char *file, unsigned line,
		    uint32_t errcode);
};

class LoggedError: public ClientError
{
public:
	template <typename ... Args>
	LoggedError(const char *file, unsigned line, uint32_t errcode, Args ... args)
		: ClientError(file, line, errcode, args...)
	{
		/* TODO: actually calls ClientError::log */
		log();
	}
};

/**
 * A special type of exception which must be used
 * for all access denied errors, since it invokes audit triggers.
 */
class AccessDeniedError: public ClientError
{
public:
	AccessDeniedError(const char *file, unsigned int line,
			  const char *access_type, const char *object_type,
			  const char *object_name, const char *user_name,
			  bool run_trigers = true);

	~AccessDeniedError()
	{
		free(m_object_name);
		free(m_object_type);
		free(m_access_type);
	}

	const char *
	object_type()
	{
		return m_object_type;
	}

	const char *
	object_name()
	{
		return m_object_name?:"(nil)";
	}

	const char *
	access_type()
	{
		return m_access_type;
	}

private:
	/** Type of object the required access was denied to */
	char *m_object_type;
	/** Name of object the required access was denied to */
	char *m_object_name;
	/** Type of declined access */
	char *m_access_type;
};

/**
 * XlogError is raised when there is an error with contents
 * of the data directory or a log file. A special subclass
 * of exception is introduced to gracefully skip such errors
 * in force_recovery = true mode.
 */
struct XlogError: public Exception
{
	XlogError(const char *file, unsigned line, const char *format,
		  va_list ap)
		:Exception(&type_XlogError, file, line)
	{
		error_vformat_msg(this, format, ap);
	}
	XlogError(const struct type_info *type, const char *file,
		  unsigned line)
		:Exception(type, file, line)
	{
	}

	virtual void raise() { throw this; }
};

struct XlogGapError: public XlogError
{
	XlogGapError(const char *file, unsigned line,
		     const struct vclock *from, const struct vclock *to);
	XlogGapError(const char *file, unsigned line,
		     const char *msg);

	virtual void raise() { throw this; }
};

class CustomError: public ClientError
{
public:
	CustomError(const char *file, unsigned int line,
		    const char *custom_type, uint32_t errcode);

	virtual void log() const;

	const char*
	custom_type()
	{
		return m_custom_type;
	}
private:
	/** Custom type name. */
	char m_custom_type[64];
};

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_ERROR_H_INCLUDED */
