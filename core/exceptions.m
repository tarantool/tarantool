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

- init:(const char *)file:(unsigned)line reason:(const char *)reason
{
	[super init];

	_file = file;
	_line = line;

	_reason = reason;

	return self;
}

- init:(const char *)file:(unsigned)line
{
	return [self init:file:line reason:"unknown"];
}

@end

