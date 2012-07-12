#include <stdio.h>
#include <objc/Object.h>
#include <objc/runtime.h>

@interface Exception: Object {
}
+ (id) new;
@end

@implementation Exception
+ (id) new
{
	return class_createInstance(self, 0);
}
@end

void
throw_exception(void)
{
	printf("throw\n");
	@throw [Exception new];
}

void
test(void)
{
	@try {
		throw_exception();
	} @finally {
		printf("internal finally\n");
	}
}

int
main(int ac, char **av)
{
	printf("start\n");
	@try {
		test();
	} @catch(id e) {
		printf("catch\n");
	} @finally {
		printf("external finally\n");
	}
	return 0;
}
