#include <time.h>
#include <stdio.h>
#include <objc/runtime.h>
#include <assert.h>
#include <string.h>
#include <class.h>
#include <stdarg.h>

//#define assert(x) if (!(x)) { printf("Failed %d\n", __LINE__); }

id objc_msgSend(id, SEL, ...);

typedef struct { int a,b,c,d,e; } s;
s objc_msgSend_stret(id, SEL, ...);
@interface Fake
- (int)izero;
- (float)fzero;
- (double)dzero;
- (long double)ldzero;
@end

Class TestCls;
@interface Test { id isa; }@end
@implementation Test 
- foo
{
	assert((id)1 == self);
	assert(strcmp("foo", sel_getName(_cmd)) == 0);
	return (id)0x42;
}
+ foo
{
	assert(TestCls == self);
	assert(strcmp("foo", sel_getName(_cmd)) == 0);
	return (id)0x42;
}
+ (s)sret
{
	assert(TestCls == self);
	assert(strcmp("sret", sel_getName(_cmd)) == 0);
	s st = {1,2,3,4,5};
	return st;
}
- (s)sret
{
	assert((id)3 == self);
	assert(strcmp("sret", sel_getName(_cmd)) == 0);
	s st = {1,2,3,4,5};
	return st;
}
+ (void)printf: (const char*)str, ...
{
	va_list ap;
	char *s;

	va_start(ap, str);

	vasprintf(&s, str, ap);
	va_end(ap);
	//fprintf(stderr, "String: '%s'\n", s);
	assert(strcmp(s, "Format string 42 42.000000\n") ==0);
}
+ (void)initialize
{
	[self printf: "Format %s %d %f%c", "string", 42, 42.0, '\n'];
	@throw self;
}
+ nothing { return 0; }
@end
int main(void)
{
	TestCls = objc_getClass("Test");
	int exceptionThrown = 0;
	@try {
		objc_msgSend(TestCls, @selector(foo));
	} @catch (id e)
	{
		assert((TestCls == e) && "Exceptions propagate out of +initialize");
		exceptionThrown = 1;
	}
	assert(exceptionThrown && "An exception was thrown");
	assert((id)0x42 == objc_msgSend(TestCls, @selector(foo)));
	objc_msgSend(TestCls, @selector(nothing));
	objc_msgSend(TestCls, @selector(missing));
	assert(0 == objc_msgSend(0, @selector(nothing)));
	id a = objc_msgSend(objc_getClass("Test"), @selector(foo));
	assert((id)0x42 == a);
	a = objc_msgSend(TestCls, @selector(foo));
	assert((id)0x42 == a);
	assert(objc_registerSmallObjectClass_np(objc_getClass("Test"), 1));
	a = objc_msgSend((id)01, @selector(foo));
	assert((id)0x42 == a);
	s ret = objc_msgSend_stret(TestCls, @selector(sret));
	assert(ret.a == 1);
	assert(ret.b == 2);
	assert(ret.c == 3);
	assert(ret.d == 4);
	assert(ret.e == 5);
	if (sizeof(id) == 8)
	{
		assert(objc_registerSmallObjectClass_np(objc_getClass("Test"), 3));
		ret = objc_msgSend_stret((id)3, @selector(sret));
		assert(ret.a == 1);
		assert(ret.b == 2);
		assert(ret.c == 3);
		assert(ret.d == 4);
		assert(ret.e == 5);
	}
	Fake *f = nil;
	assert(0 == [f izero]);
	assert(0 == [f dzero]);
	assert(0 == [f ldzero]);
	assert(0 == [f fzero]);
#ifdef BENCHMARK
	clock_t c1, c2;
	c1 = clock();
	for (int i=0 ; i<100000000 ; i++)
	{
		[TestCls nothing];
	}
	c2 = clock();
	printf("Traditional message send took %f seconds. \n", 
		((double)c2 - (double)c1) / (double)CLOCKS_PER_SEC);
	c1 = clock();
	for (int i=0 ; i<100000000 ; i++)
	{
		objc_msgSend(TestCls, @selector(nothing));
	}
	c2 = clock();
	printf("objc_msgSend() message send took %f seconds. \n", 
		((double)c2 - (double)c1) / (double)CLOCKS_PER_SEC);
	IMP nothing = objc_msg_lookup(TestCls, @selector(nothing));
	c1 = clock();
	for (int i=0 ; i<100000000 ; i++)
	{
		nothing(TestCls, @selector(nothing));
	}
	c2 = clock();
	printf("Direct IMP call took %f seconds. \n", 
		((double)c2 - (double)c1) / (double)CLOCKS_PER_SEC);
#endif
	return 0;
}
