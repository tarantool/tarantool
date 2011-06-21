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
#import <objc/Object.h>

/** The base class for all exceptions.
 *
 * Note: implements garbage collection (see +alloc
 * implementation).
 */

@interface tnt_Exception: Object {
	@public
		const char *file;
		unsigned line;
		const char *reason;
}

+ alloc;

- init:(const char *)p_file:(unsigned)p_line reason:(const char*)p_reason;
- init:(const char *)p_file:(unsigned)p_line;
@end

#define tnt_raise(class, message) {					\
	say_debug("tnt_raise %s at %s:%i", #class, __FILE__, __LINE__);	\
	@throw [[class alloc] init:__FILE__:__LINE__ message];		\
}

#endif /* TARANTOOL_EXCEPTION_H_INCLUDED */
