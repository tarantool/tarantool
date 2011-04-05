#include <exceptions.h>

@implementation TNTException
+(id) withReason:(const char *)str
{
	static id e = nil;

	if (![e isKindOf:self]) {
		[e free];
		e = [[self alloc] init];
	}

	return [e setReason:str];
}

-(void) init
{
	[super init];

	reason = "";
}

-(TNTException *) setReason:(const char *)str
{
	reason = str;

	return self;
}

-(const char *) Reason
{
	return reason;
}
@end
