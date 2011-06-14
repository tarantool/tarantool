#ifndef TARANTOOL_EXCEPTIONS_H
#define TARANTOOL_EXCEPTIONS_H

#include <objc/Object.h>

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

#endif
