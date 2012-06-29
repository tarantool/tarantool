#include "objc/runtime.h"
#include "selector.h"
#include "class.h"
#include "protocol.h"
#include "ivar.h"
#include "method_list.h"
#include "lock.h"
#include "dtable.h"
#include "gc_ops.h"

/* Make glibc export strdup() */

#if defined __GLIBC__
	#define __USE_BSD 1
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

struct objc_slot *objc_get_slot(Class cls, SEL selector);
#define CHECK_ARG(arg) if (0 == arg) { return 0; }

/**
 * Calls C++ destructors in the correct order.
 */
PRIVATE void call_cxx_destruct(id obj)
{
	static SEL cxx_destruct;
	if (NULL == cxx_destruct)
	{
		cxx_destruct = sel_registerName(".cxx_destruct");
	}
	// Don't call object_getClass(), because we want to get hidden classes too
	Class cls = classForObject(obj);

	while (cls)
	{
		struct objc_slot *slot = objc_get_slot(cls, cxx_destruct);
		cls = Nil;
		if (NULL != slot)
		{
			cls = slot->owner->super_class;
			slot->method(obj, cxx_destruct);
		}
	}
}

static void call_cxx_construct_for_class(Class cls, id obj)
{
	static SEL cxx_construct;
	if (NULL == cxx_construct)
	{
		cxx_construct = sel_registerName(".cxx_contruct");
	}
	struct objc_slot *slot = objc_get_slot(cls, cxx_construct);
	if (NULL != slot)
	{
		cls = slot->owner->super_class;
		if (Nil != cls)
		{
			call_cxx_construct_for_class(cls, obj);
		}
		slot->method(obj, cxx_construct);
	}
}

PRIVATE void call_cxx_construct(id obj)
{
	call_cxx_construct_for_class(classForObject(obj), obj);
}

/**
 * Looks up the instance method in a specific class, without recursing into
 * superclasses.
 */
static Method class_getInstanceMethodNonrecursive(Class aClass, SEL aSelector)
{
	for (struct objc_method_list *methods = aClass->methods;
		methods != NULL ; methods = methods->next)
	{
		for (int i=0 ; i<methods->count ; i++)
		{
			Method method = &methods->methods[i];
			if (sel_isEqual(method->selector, aSelector))
			{
				return method;
			}
		}
	}
	return NULL;
}

static void objc_updateDtableForClassContainingMethod(Method m)
{
	Class nextClass = Nil;
	void *state = NULL;
	SEL sel = method_getName(m);
	while (Nil != (nextClass = objc_next_class(&state)))
	{
		if (class_getInstanceMethodNonrecursive(nextClass, sel) == m)
		{
			objc_update_dtable_for_class(nextClass);
			return;
		}
	}
}

BOOL class_addIvar(Class cls, const char *name, size_t size, uint8_t alignment,
		const char *types)
{
	CHECK_ARG(cls);
	CHECK_ARG(name);
	CHECK_ARG(types);
	// You can't add ivars to initialized classes.  Note: We can't use the
	// resolved flag here because class_getInstanceVariable() sets it.
	if (objc_test_class_flag(cls, objc_class_flag_initialized))
	{
		return NO;
	}

	if (class_getInstanceVariable(cls, name) != NULL)
	{
		return NO;
	}

	struct objc_ivar_list *ivarlist = cls->ivars;

	if (NULL == ivarlist)
	{
		cls->ivars = malloc(sizeof(struct objc_ivar_list) + sizeof(struct objc_ivar));
		cls->ivars->count = 1;
	}
	else
	{
		ivarlist->count++;
		// objc_ivar_list contains one ivar.  Others follow it.
		cls->ivars = realloc(ivarlist, sizeof(struct objc_ivar_list) +
				(ivarlist->count) * sizeof(struct objc_ivar));
	}
	Ivar ivar = &cls->ivars->ivar_list[cls->ivars->count - 1];
	ivar->name = strdup(name);
	ivar->type = strdup(types);
	// Round up the offset of the ivar so it is correctly aligned.
	long offset = cls->instance_size >> alignment;

	if (offset << alignment != cls->instance_size)
	{
		offset++;
	}
	offset <<= alignment;

	ivar->offset = offset;
	// Increase the instance size to make space for this.
	cls->instance_size = ivar->offset + size;
	return YES;
}

BOOL class_addMethod(Class cls, SEL name, IMP imp, const char *types)
{
	CHECK_ARG(cls);
	CHECK_ARG(name);
	CHECK_ARG(imp);
	CHECK_ARG(types);
	const char *methodName = sel_getName(name);
	struct objc_method_list *methods;
	for (methods=cls->methods; methods!=NULL ; methods=methods->next)
	{
		for (int i=0 ; i<methods->count ; i++)
		{
			Method method = &methods->methods[i];
			if (strcmp(sel_getName(method->selector), methodName) == 0)
			{
				return NO;
			}
		}
	}

	methods = malloc(sizeof(struct objc_method_list) + sizeof(struct objc_method));
	methods->next = cls->methods;
	cls->methods = methods;

	methods->count = 1;
	methods->methods[0].selector = sel_registerTypedName_np(methodName, types);
	methods->methods[0].types = strdup(types);
	methods->methods[0].imp = imp;

	if (objc_test_class_flag(cls, objc_class_flag_resolved))
	{
		add_method_list_to_class(cls, methods);
	}

	return YES;
}

BOOL class_addProtocol(Class cls, Protocol *protocol)
{
	CHECK_ARG(cls);
	CHECK_ARG(protocol);
	if (class_conformsToProtocol(cls, protocol)) { return NO; }
	struct objc_protocol_list *protocols =
		malloc(sizeof(struct objc_protocol_list) + sizeof(Protocol2*));
	if (protocols == NULL) { return NO; }
	protocols->next = cls->protocols;
	protocols->count = 1;
	protocols->list[0] = (Protocol2*)protocol;
	cls->protocols = protocols;

	return YES;
}

Ivar * class_copyIvarList(Class cls, unsigned int *outCount)
{
	CHECK_ARG(cls);
	struct objc_ivar_list *ivarlist = NULL;
	unsigned int count = 0;
	unsigned int index;
	Ivar *list;

	if (Nil != cls)
	{
		ivarlist = cls->ivars;
	}
	if (ivarlist != NULL)
	{
		count = ivarlist->count;
	}
	if (outCount != NULL)
	{
		*outCount = count;
	}
	if (count == 0)
	{
		return NULL;
	}

	list = malloc((count + 1) * sizeof(struct objc_ivar *));
	list[count] = NULL;
	count = 0;
	for (index = 0; index < ivarlist->count; index++)
	{
		list[count++] = &ivarlist->ivar_list[index];
	}

	return list;
}

Method * class_copyMethodList(Class cls, unsigned int *outCount)
{
	CHECK_ARG(cls);
	unsigned int count = 0;
	Method *list;
	struct objc_method_list *methods;

	if (cls != NULL)
	{
		for (methods = cls->methods; methods != NULL; methods = methods->next)
		{
			count += methods->count;
		}
	}
	if (outCount != NULL)
	{
		*outCount = count;
	}
	if (count == 0)
	{
		return NULL;
	}

	list = malloc((count + 1) * sizeof(struct objc_method *));
	list[count] = NULL;
	count = 0;
	for (methods = cls->methods; methods != NULL; methods = methods->next)
	{
		unsigned int	index;
		for (index = 0; index < methods->count; index++)
		{
			list[count++] = &methods->methods[index];
		}
	}

	return list;
}

Protocol*__unsafe_unretained* class_copyProtocolList(Class cls, unsigned int *outCount)
{
	CHECK_ARG(cls);
	struct objc_protocol_list *protocolList = NULL;
	struct objc_protocol_list *list;
	unsigned int count = 0;
	Protocol **protocols;

	if (Nil != cls)
	{
		protocolList = cls->protocols;
	}
	for (list = protocolList; list != NULL; list = list->next)
	{
		count += list->count;
	}
	if (outCount != NULL)
	{
		*outCount = count;
	}
	if (count == 0)
	{
		return NULL;
	}

	protocols = malloc((count + 1) * sizeof(Protocol *));
	protocols[count] = NULL;
	count = 0;
	for (list = protocolList; list != NULL; list = list->next)
	{
		memcpy(&protocols[count], list->list, list->count * sizeof(Protocol *));
		count += list->count;
	}
	return protocols;
}

id class_createInstance(Class cls, size_t extraBytes)
{
	CHECK_ARG(cls);
	if (sizeof(id) == 4)
	{
		if (cls == SmallObjectClasses[0])
		{
			return (id)1;
		}
	}
	else
	{
		for (int i=0 ; i<4 ; i++)
		{
			if (cls == SmallObjectClasses[i])
			{
				return (id)(uintptr_t)((i<<1)+1);
			}
		}
	}

	if (Nil == cls)	{ return nil; }
	id obj = gc->allocate_class(cls, extraBytes);
	obj->isa = cls;
	call_cxx_construct(obj);
	return obj;
}

id object_copy(id obj, size_t size)
{
	Class cls = object_getClass(obj);
	id cpy = class_createInstance(cls, size - class_getInstanceSize(cls));
	memcpy(((char*)cpy + sizeof(id)), ((char*)obj + sizeof(id)), size - sizeof(id));
	return cpy;
}

id object_dispose(id obj)
{
	call_cxx_destruct(obj);
	gc->free_object(obj);
	return nil;
}

Method class_getInstanceMethod(Class aClass, SEL aSelector)
{
	CHECK_ARG(aClass);
	CHECK_ARG(aSelector);
	// If the class has a dtable installed, then we can use the fast path
	if (classHasInstalledDtable(aClass))
	{
		// Do a dtable lookup to find out which class the method comes from.
		struct objc_slot *slot = objc_get_slot(aClass, aSelector);
		if (NULL == slot)
		{
			slot = objc_get_slot(aClass, sel_registerName(sel_getName(aSelector)));
			if (NULL == slot)
			{
				return NULL;
			}
		}

		// Now find the typed variant of the selector, with the correct types.
		aSelector = slot->selector;

		// Then do the slow lookup to find the method.
		return class_getInstanceMethodNonrecursive(slot->owner, aSelector);
	}
	Method m = class_getInstanceMethodNonrecursive(aClass, aSelector);
	if (NULL != m)
	{
		return m;
	}
	return class_getInstanceMethod(class_getSuperclass(aClass), aSelector);
}

Method class_getClassMethod(Class aClass, SEL aSelector)
{
	return class_getInstanceMethod(object_getClass((id)aClass), aSelector);
}

Ivar class_getClassVariable(Class cls, const char* name)
{
	// Note: We don't have compiler support for cvars in ObjC
	return class_getInstanceVariable(object_getClass((id)cls), name);
}

size_t class_getInstanceSize(Class cls)
{
	if (Nil == cls) { return 0; }
	return cls->instance_size;
}

Ivar class_getInstanceVariable(Class cls, const char *name)
{
	if (name != NULL)
	{
		while (cls != Nil)
		{
			struct objc_ivar_list *ivarlist = cls->ivars;

			if (ivarlist != NULL)
			{
				for (int i = 0; i < ivarlist->count; i++)
				{
					Ivar ivar = &ivarlist->ivar_list[i];
					if (strcmp(ivar->name, name) == 0)
					{
						return ivar;
					}
				}
			}
			cls = class_getSuperclass(cls);
		}
	}
	return NULL;
}

// The format of the char* is undocumented.  This function is only ever used in
// conjunction with class_setIvarLayout().
const char *class_getIvarLayout(Class cls)
{
	CHECK_ARG(cls);
	return (char*)cls->ivars;
}


const char * class_getName(Class cls)
{
	if (Nil == cls) { return "nil"; }
	return cls->name;
}

int class_getVersion(Class theClass)
{
	CHECK_ARG(theClass);
	return theClass->version;
}

const char *class_getWeakIvarLayout(Class cls)
{
	assert(0 && "Weak ivars not supported");
	return NULL;
}

BOOL class_isMetaClass(Class cls)
{
	CHECK_ARG(cls);
	return objc_test_class_flag(cls, objc_class_flag_meta);
}

IMP class_replaceMethod(Class cls, SEL name, IMP imp, const char *types)
{
	if (Nil == cls) { return (IMP)0; }
	SEL sel = sel_registerTypedName_np(sel_getName(name), types);
	Method method = class_getInstanceMethodNonrecursive(cls, sel);
	if (method == NULL)
	{
		class_addMethod(cls, sel, imp, types);
		return NULL;
	}
	IMP old = (IMP)method->imp;
	method->imp = imp;

	if (objc_test_class_flag(cls, objc_class_flag_resolved))
	{
		objc_update_dtable_for_class(cls);
	}

	return old;
}


void class_setIvarLayout(Class cls, const char *layout)
{
	if ((Nil == cls) || (NULL == layout)) { return; }
	struct objc_ivar_list *list = (struct objc_ivar_list*)layout;
	size_t listsize = sizeof(struct objc_ivar_list) +
			sizeof(struct objc_ivar) * (list->count);
	cls->ivars = malloc(listsize);
	memcpy(cls->ivars, list, listsize);
}

__attribute__((deprecated))
Class class_setSuperclass(Class cls, Class newSuper)
{
	CHECK_ARG(cls);
	CHECK_ARG(newSuper);
	if (Nil == cls) { return Nil; }
	Class oldSuper = cls->super_class;
	cls->super_class = newSuper;
	return oldSuper;
}

void class_setVersion(Class theClass, int version)
{
	if (Nil == theClass) { return; }
	theClass->version = version;
}

void class_setWeakIvarLayout(Class cls, const char *layout)
{
	assert(0 && "Not implemented");
}

const char * ivar_getName(Ivar ivar)
{
	CHECK_ARG(ivar);
	return ivar->name;
}

ptrdiff_t ivar_getOffset(Ivar ivar)
{
	CHECK_ARG(ivar);
	return ivar->offset;
}

const char * ivar_getTypeEncoding(Ivar ivar)
{
	CHECK_ARG(ivar);
	return ivar->type;
}


void method_exchangeImplementations(Method m1, Method m2)
{
	if (NULL == m1 || NULL == m2) { return; }
	IMP tmp = (IMP)m1->imp;
	m1->imp = m2->imp;
	m2->imp = tmp;
	objc_updateDtableForClassContainingMethod(m1);
	objc_updateDtableForClassContainingMethod(m2);
}

IMP method_getImplementation(Method method)
{
	if (NULL == method) { return (IMP)NULL; }
	return (IMP)method->imp;
}

SEL method_getName(Method method)
{
	if (NULL == method) { return (SEL)NULL; }
	return (SEL)method->selector;
}


IMP method_setImplementation(Method method, IMP imp)
{
	if (NULL == method) { return (IMP)NULL; }
	IMP old = (IMP)method->imp;
	method->imp = old;
	objc_updateDtableForClassContainingMethod(method);
	return old;
}

id objc_getRequiredClass(const char *name)
{
	CHECK_ARG(name);
	id cls = objc_getClass(name);
	if (nil == cls)
	{
		abort();
	}
	return cls;
}

static void freeMethodLists(Class aClass)
{
	struct objc_method_list *methods = aClass->methods;
	while(methods != NULL)
	{
		for (int i=0 ; i<methods->count ; i++)
		{
			free((void*)methods->methods[i].types);
		}
		struct objc_method_list *current = methods;
	   	methods = methods->next;
		free(current);
	}
}

static void freeIvarLists(Class aClass)
{
	struct objc_ivar_list *ivarlist = aClass->ivars;
	if (NULL == ivarlist) { return; }

	for (int i=0 ; i<ivarlist->count ; i++)
	{
		Ivar ivar = &ivarlist->ivar_list[i];
		free((void*)ivar->type);
		free((void*)ivar->name);
	}
	free(ivarlist);
}

/*
 * Removes a class from the subclass list found on its super class.
 * Must be called with the objc runtime mutex locked.
 */
static inline void safe_remove_from_subclass_list(Class cls)
{
	// If this class hasn't been added to the class hierarchy, then this is easy
	if (!objc_test_class_flag(cls, objc_class_flag_resolved)) { return; }
	Class sub = cls->super_class->subclass_list;
	if (sub == cls)
	{
		cls->super_class->subclass_list = cls->sibling_class;
	}
	else
	{
		while (sub != NULL)
		{
			if (sub->sibling_class == cls)
			{
				sub->sibling_class = cls->sibling_class;
				break;
			}
			sub = sub->sibling_class;
		}
	}
}

void objc_disposeClassPair(Class cls)
{
	if (0 == cls) { return; }
	Class meta = ((id)cls)->isa;
	// Remove from the runtime system so nothing tries updating the dtable
	// while we are freeing the class.
	{
		LOCK_RUNTIME_FOR_SCOPE();
		safe_remove_from_subclass_list(meta);
		safe_remove_from_subclass_list(cls);
	}

	// Free the method and ivar lists.
	freeMethodLists(cls);
	freeMethodLists(meta);
	freeIvarLists(cls);
	if (cls->dtable != uninstalled_dtable)
	{
		free_dtable(cls->dtable);
	}
	if (meta->dtable != uninstalled_dtable)
	{
		free_dtable(meta->dtable);
	}

	// Free the class and metaclass
	gc->free(meta);
	gc->free(cls);
}

Class objc_allocateClassPair(Class superclass, const char *name, size_t extraBytes)
{
	// Check the class doesn't already exist.
	if (nil != objc_lookUpClass(name)) { return Nil; }

	Class newClass = gc->malloc(sizeof(struct objc_class) + extraBytes);

	if (Nil == newClass) { return Nil; }

	// Create the metaclass
	Class metaClass = gc->malloc(sizeof(struct objc_class));

	if (Nil == superclass)
	{
		/*
		 * Metaclasses of root classes are precious little flowers and work a
		 * little differently:
		 */
		metaClass->isa = metaClass;
		metaClass->super_class = newClass;
	}
	else
	{
		// Initialize the metaclass
		// Set the meta-metaclass pointer to the name.  The runtime will fix this
		// in objc_resolve_class().
		metaClass->isa = (Class)superclass->isa->isa->name;
		metaClass->super_class = superclass->isa;
	}
	metaClass->name = strdup(name);
	metaClass->info = objc_class_flag_meta | objc_class_flag_user_created |
		objc_class_flag_new_abi;
	metaClass->dtable = uninstalled_dtable;
	metaClass->instance_size = sizeof(struct objc_class);

	// Set up the new class
	newClass->isa = metaClass;
	// Set the superclass pointer to the name.  The runtime will fix this when
	// the class links are resolved.
	newClass->super_class = (Nil == superclass) ? Nil : (Class)(superclass->name);

	newClass->name = strdup(name);
	newClass->info = objc_class_flag_class | objc_class_flag_user_created |
		objc_class_flag_new_abi;
	newClass->dtable = uninstalled_dtable;

	if (Nil == superclass)
	{
		newClass->instance_size = sizeof(struct objc_class);
	}
	else
	{
		newClass->instance_size = superclass->instance_size;
	}

	return newClass;
}


void *object_getIndexedIvars(id obj)
{
	CHECK_ARG(obj);
	size_t size = classForObject(obj)->instance_size;
	if ((0 == size) && class_isMetaClass(classForObject(obj)))
	{
		Class cls = (Class)obj;
		if (objc_test_class_flag(cls, objc_class_flag_new_abi))
		{
			size = sizeof(struct objc_class);
		}
		else
		{
			size = sizeof(struct legacy_abi_objc_class);
		}
	}
	return ((char*)obj) + size;
}

Class object_getClass(id obj)
{
	CHECK_ARG(obj);
	Class isa = classForObject(obj);
	while ((Nil != isa) && objc_test_class_flag(isa, objc_class_flag_hidden_class))
	{
		isa = isa->super_class;
	}
	return isa;
}

Class object_setClass(id obj, Class cls)
{
	CHECK_ARG(obj);
	// If this is a small object, then don't set its class.
	uintptr_t addr = (uintptr_t)obj;
	if (addr & 1) { return classForObject(obj); }
	Class oldClass =  obj->isa;
	obj->isa = cls;
	return oldClass;
}

const char *object_getClassName(id obj)
{
	CHECK_ARG(obj);
	return class_getName(object_getClass(obj));
}

void objc_registerClassPair(Class cls)
{
	LOCK_RUNTIME_FOR_SCOPE();
	class_table_insert(cls);
}

