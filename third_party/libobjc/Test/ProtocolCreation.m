#import <objc/runtime.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

@protocol Test2 @end

int main(void)
{
	Protocol *p = objc_allocateProtocol("Test");
	protocol_addMethodDescription(p, @selector(someMethod), "@:", YES, NO);
	assert(objc_getProtocol("Test2"));
	protocol_addProtocol(p, objc_getProtocol("Test2"));
	objc_property_attribute_t attrs[] = { {"T", "@" } };
	protocol_addProperty(p, "foo", attrs, 1, YES, YES);
	objc_registerProtocol(p);
	Protocol *p1 = objc_getProtocol("Test");
	assert(p == p1);
	struct objc_method_description d = protocol_getMethodDescription(p1, @selector(someMethod), YES, NO);
	assert(strcmp(sel_getName(d.name), "someMethod") == 0);
	assert(strcmp((d.types), "@:") == 0);
	assert(protocol_conformsToProtocol(p1, objc_getProtocol("Test2")));
	unsigned int count;
	objc_property_t *props = protocol_copyPropertyList(p1, &count);
	assert(count == 1);
	assert(strcmp("T@,Vfoo", property_getAttributes(*props)) == 0);
	return 0;
}
