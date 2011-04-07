#ifndef TARANTOOL_EXCEPTIONS_H
#define TARANTOOL_EXCEPTIONS_H

#include <objc/Object.h>

@interface tnt_Exception: Object {
	@public
		const char *_file;
		unsigned _line;
		const char *_reason;
}

+ alloc;

- init:(const char *)file:(unsigned)line reason:(const char*)reason;
- init:(const char *)file:(unsigned)line;
@end

#define tnt_raise(class, message) {					\
	say_debug("tnt_raise %s at %s:%i", #class, __FILE__, __LINE__);	\
	@throw [[class alloc] init:__FILE__:__LINE__ message];		\
}

#endif
