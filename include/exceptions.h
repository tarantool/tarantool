#ifndef TARANTOOL_EXCEPTIONS_H
#define TARANTOOL_EXCEPTIONS_H

#include <objc/Object.h>

@interface TNTException: Object {
	const char *reason;
}

+(id) withReason:(const char *)str;

-(TNTException *) setReason:(const char *)str;
-(const char *) Reason;
@end

#endif
