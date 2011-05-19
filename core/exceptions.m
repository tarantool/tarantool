#include <exceptions.h>
#include <say.h>

@implementation tnt_Exception
+ alloc
{
	static __thread tnt_Exception *e = nil;

	if (![e isKindOf:self]) {
		[e free];
		e = [super alloc];
	}

	return e;
}

- init:(const char *)p_file:(unsigned)p_line reason:(const char *)p_reason
{
	[super init];

	file = p_file;
	line = p_line;

	reason = p_reason;

	return self;
}

- init:(const char *)p_file:(unsigned)p_line
{
	return [self init:p_file:p_line reason:"unknown"];
}

@end

