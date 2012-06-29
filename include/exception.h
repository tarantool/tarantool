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
#import "object.h"
#include <stdarg.h>
#include "errcode.h"
#include "say.h"

/** The base class for all exceptions.
 *
 * Note: implements garbage collection (see +alloc
 * implementation).
 */
@interface tnt_Exception: tnt_Object {
	@public
		const char *file;
		unsigned line;
}
+ (id) alloc;
@end


/** Internal error resulting from a failed system call.
 */
@interface SystemError: tnt_Exception {
	@public
		/* errno code */
		int errnum;
		/* error description */
		char errmsg[TNT_ERRMSG_MAX];
}

- (id) init: (const char *)msg, ...;
- (id) init: (int)errnum_arg: (const char *)format, ...;
- (id) init: (int)errnum_arg: (const char *)format: (va_list)ap;
- (void) log;
@end


/** Errors that should make it to the client.
 */
@interface ClientError: tnt_Exception {
	@public
		uint32_t errcode;
		char errmsg[TNT_ERRMSG_MAX];
}

- (id) init: (uint32_t)errcode_, ...;
- (id) init: (uint32_t)errcode_ args: (va_list)ap;
@end


/** Additionally log this error in the log file. */
@interface LoggedError: ClientError
- (id) init: (uint32_t)errcode, ...;
@end


/** A handy wrapper for ER_ILLEGAL_PARAMS, which is used very
 * often.
 */
@interface IllegalParams: LoggedError
- (id) init: (const char *)msg;
@end

/** ER_INJECTION wrapper. */
@interface ErrorInjection: LoggedError
- (id) init: (const char *)msg;
@end

/**
 * A helper macro to add __FILE__ and __LINE__ information to
 * raised exceptions.
 *
 * Usage:
 *
 * tnt_raise(tnt_Exception);
 * tnt_raise(LoggedError, :"invalid argument %d", argno);
 */
#define tnt_raise(...) tnt_raise0(__VA_ARGS__)
#define tnt_raise0(class, ...) do {					\
	say_debug("%s at %s:%i", #class, __FILE__, __LINE__);		\
	class *exception = [class alloc];				\
	exception->file = __FILE__;					\
	exception->line = __LINE__;					\
	[exception init __VA_ARGS__];					\
	@throw exception;						\
} while (0)

#endif /* TARANTOOL_EXCEPTION_H_INCLUDED */
