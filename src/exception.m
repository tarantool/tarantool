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


@implementation tnt_Exception
+ (id) alloc
{
	static __thread tnt_Exception *e = nil;

	if ([e isKindOf:self]) {
		*(Class *) e = self;
	} else {
		[e free];
		e = [super alloc];
	}
	return e;
}
@end


@implementation SystemError

- (id) init: (const char *) format, ...
{
	va_list ap;
	va_start(ap, format);
	[self init: errno :format :ap];
	va_end(ap);
	return self;
}

- (id) init: (int)errnum_arg: (const char *)format, ...
{
	va_list ap;
	va_start(ap, format);
	[self init: errnum_arg :format: ap];
	va_end(ap);
	return self;
}

- (id) init: (int)errnum_arg :(const char *)format: (va_list)ap
{
	self = [super init];
	errnum = errnum_arg;
	vsnprintf(errmsg, sizeof(errmsg), format, ap);
	return self;
}

- (void) log
{
	say(S_ERROR, strerror(errnum), "%s", errmsg);
}

@end


@implementation ClientError
- (id) init: (uint32_t)errcode_, ...
{
	va_list ap;
	va_start(ap, errcode_);
	[self init: errcode_ args: ap];
	va_end(ap);

	return self;
}


- (id) init: (uint32_t)errcode_ args: (va_list)ap
{
	[super init];
	errcode = errcode_;
	vsnprintf(errmsg, sizeof(errmsg), tnt_errcode_desc(errcode), ap);
	return self;
}
@end


@implementation LoggedError
- (id) init: (uint32_t) errcode_, ...
{
	va_list ap;
	va_start(ap, errcode_);
	[super init: errcode_ args: ap];

	say_error("%s at %s:%d, %s", [self name], file, line, errmsg);

	return self;
}
@end


@implementation IllegalParams
- (id) init: (const char*) msg
{
	return [super init: ER_ILLEGAL_PARAMS, msg];
}
@end

@implementation ErrorInjection
- (id) init: (const char*) msg
{
	return [super init: ER_INJECTION, msg];
}
@end
