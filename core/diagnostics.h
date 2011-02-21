#ifndef TARANTOOL_CORE_DIAGNOSTICS_H_INCLUDED
#define TARANTOOL_CORE_DIAGNOSTICS_H_INCLUDED
/*
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met: 1. Redistributions of source code must
 * retain the above copyright notice, this list of conditions and
 * the following disclaimer.  2. Redistributions in binary form
 * must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * This is used globally in the program to pass around information
 * about execution errors. Each fiber has its own error context,
 * setting an error in one doesn't affect another.
 */

struct Error
{
	/** Most often contains system errno. */
	int code;
	/** Text description of the error. Can be NULL. */
	const char *msg;
};

/**
 * Set the last error in the current execution context (fiber).
 * If another error was already set, it's overwritten.
 *
 * @param code  Error code.
 * @todo: think how to distinguish errno and tarantool codes here.
 * @param message  Optional text message. Can be NULL.
 */
void diag_set_error(int code, const char *msg);

/** Return the last error. Return NULL if no error.
 */
struct Error *diag_get_last_error();

/** Clear the last error, if any.
 */
void diag_clear();

#endif /* TARANTOOL_CORE_DIAGNOSTICS_H_INCLUDED */
