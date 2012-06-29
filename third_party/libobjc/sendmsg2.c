#include "objc/runtime.h"
#include "lock.h"
#include "dtable.h"
#include "selector.h"
#include "loader.h"
#include "objc/hooks.h"
#include <stdint.h>
#include <stdio.h>

void objc_send_initialize(id object);

static long long nil_method(id self, SEL _cmd) { return 0; }
static long double nil_method_D(id self, SEL _cmd) { return 0; }
static double nil_method_d(id self, SEL _cmd) { return 0; }
static float nil_method_f(id self, SEL _cmd) { return 0; }

static struct objc_slot nil_slot = { Nil, Nil, 0, 1, (IMP)nil_method };
static struct objc_slot nil_slot_D = { Nil, Nil, 0, 1, (IMP)nil_method_D };
static struct objc_slot nil_slot_d = { Nil, Nil, 0, 1, (IMP)nil_method_d };
static struct objc_slot nil_slot_f = { Nil, Nil, 0, 1, (IMP)nil_method_f };

typedef struct objc_slot *Slot_t;

Slot_t objc_msg_lookup_sender(id *receiver, SEL selector, id sender);

// Default implementations of the two new hooks.  Return NULL.
static id objc_proxy_lookup_null(id receiver, SEL op) { return nil; }
static Slot_t objc_msg_forward3_null(id receiver, SEL op) { return &nil_slot; }

id (*objc_proxy_lookup)(id receiver, SEL op) = objc_proxy_lookup_null;
Slot_t (*__objc_msg_forward3)(id receiver, SEL op) = objc_msg_forward3_null;

#ifndef NO_SELECTOR_MISMATCH_WARNINGS
static struct objc_slot* objc_selector_type_mismatch(Class cls, SEL
		selector, Slot_t result)
{
	fprintf(stderr, "Calling [%s %c%s] with incorrect signature.  "
			"Method has %s, selector has %s\n",
			cls->name,
			class_isMetaClass(cls) ? '+' : '-',
			sel_getName(selector),
			result->types,
			sel_getType_np(selector));
	return result;
}
#else
static struct objc_slot* objc_selector_type_mismatch(Class cls, SEL
		selector, Slot_t result)
{
	return result;
}
#endif

struct objc_slot* (*_objc_selector_type_mismatch)(Class cls, SEL
		selector, struct objc_slot *result) = objc_selector_type_mismatch;
static 
// Uncomment for debugging
//__attribute__((noinline))
__attribute__((always_inline))
Slot_t objc_msg_lookup_internal(id *receiver,
                                SEL selector, 
                                id sender)
{
retry:;
	Class class = classForObject((*receiver));
	Slot_t result = objc_dtable_lookup(class->dtable, selector->index);
	if (UNLIKELY(0 == result))
	{
		dtable_t dtable = dtable_for_class(class);
		/* Install the dtable if it hasn't already been initialized. */
		if (dtable == uninstalled_dtable)
		{
			objc_send_initialize(*receiver);
			dtable = dtable_for_class(class);
			result = objc_dtable_lookup(dtable, selector->index);
		}
		else
		{
			// Check again incase another thread updated the dtable while we
			// weren't looking
			result = objc_dtable_lookup(dtable, selector->index);
		}
		if (0 == result)
		{
			if (!isSelRegistered(selector))
			{
				objc_register_selector(selector);
				// This should be a tail call, but GCC is stupid and won't let
				// us tail call an always_inline function.
				goto retry;
			}
			if ((result = objc_dtable_lookup(dtable, get_untyped_idx(selector))))
			{
				return _objc_selector_type_mismatch(class, selector, result);
			}
			id newReceiver = objc_proxy_lookup(*receiver, selector);
			// If some other library wants us to play forwarding games, try
			// again with the new object.
			if (nil != newReceiver)
			{
				*receiver = newReceiver;
				return objc_msg_lookup_sender(receiver, selector, sender);
			}
			if (0 == result)
			{
				result = __objc_msg_forward3(*receiver, selector);
			}
		}
	}
	return result;
}

PRIVATE
IMP slowMsgLookup(id *receiver, SEL cmd)
{
	return objc_msg_lookup_sender(receiver, cmd, nil)->method;
}

PRIVATE void logInt(void *a)
{
	fprintf(stderr, "Value: %p\n", a);
}

Slot_t (*objc_plane_lookup)(id *receiver, SEL op, id sender) =
	objc_msg_lookup_internal;

Slot_t objc_msg_lookup_sender_non_nil(id *receiver, SEL selector, id sender)
{
	return objc_msg_lookup_internal(receiver, selector, sender);
}

/**
 * New Objective-C lookup function.  This permits the lookup to modify the
 * receiver and also supports multi-dimensional dispatch based on the sender.  
 */
Slot_t objc_msg_lookup_sender(id *receiver, SEL selector, id sender)
{
	// Returning a nil slot allows the caller to cache the lookup for nil too,
	// although this is not particularly useful because the nil method can be
	// inlined trivially.
	if (UNLIKELY(*receiver == nil))
	{
		// Return the correct kind of zero, depending on the type encoding.
		if (selector->types)
		{
			const char *t = selector->types;
			// Skip type qualifiers
			while ('r' == *t || 'n' == *t || 'N' == *t || 'o' == *t ||
			       'O' == *t || 'R' == *t || 'V' == *t) 
			{
				t++;
			}
			switch (selector->types[0])
			{
				case 'D': return &nil_slot_D;
				case 'd': return &nil_slot_d;
				case 'f': return &nil_slot_f;
			}
		}
		return &nil_slot;
	}

	/*
	 * The self pointer is invalid in some code.  This test is disabled until
	 * we can guarantee that it is not (e.g. with GCKit)
	if (__builtin_expect(sender == nil
		||
		(sender->isa->info & (*receiver)->isa->info & _CLS_PLANE_AWARE),1))
	*/
	{
		return objc_msg_lookup_internal(receiver, selector, sender);
	}
	// If we are in plane-aware code
	void *senderPlaneID = *((void**)sender - 1);
	void *receiverPlaneID = *((void**)receiver - 1);
	if (senderPlaneID == receiverPlaneID)
	{
		return objc_msg_lookup_internal(receiver, selector, sender);
	}
	return objc_plane_lookup(receiver, selector, sender);
}

Slot_t objc_slot_lookup_super(struct objc_super *super, SEL selector)
{
	id receiver = super->receiver;
	if (receiver)
	{
		Class class = super->class;
		Slot_t result = objc_dtable_lookup(dtable_for_class(class),
				selector->index);
		if (0 == result)
		{
			Class class = classForObject(receiver);
			// Dtable should always be installed in the superclass
			// Unfortunately, some stupid code (PyObjC) decides to use this
			// mechanism for everything 
			if (dtable_for_class(class) == uninstalled_dtable)
			{
				if (class_isMetaClass(class))
				{
					objc_send_initialize(receiver);
				}
				else
				{
					objc_send_initialize((id)class);
				}
				objc_send_initialize((id)class);
				return objc_slot_lookup_super(super, selector);
			}
			result = &nil_slot;
		}
		return result;
	}
	else
	{
		return &nil_slot;
	}
}

////////////////////////////////////////////////////////////////////////////////
// Profiling
////////////////////////////////////////////////////////////////////////////////

/**
 * Mutex used to protect non-thread-safe parts of the profiling subsystem.
 */
static mutex_t profileLock;
/**
 * File used for writing the profiling symbol table.
 */
static FILE *profileSymbols;
/**
 * File used for writing the profiling data.
 */
static FILE *profileData;

struct profile_info 
{
	const char *module;
	int32_t callsite;
	IMP method;
};

static void profile_init(void)
{
	INIT_LOCK(profileLock);
	profileSymbols = fopen("objc_profile.symbols", "a");
	profileData = fopen("objc_profile.data", "a");
	// Write markers indicating a new run.  
	fprintf(profileSymbols, "=== NEW TRACE ===\n");
	struct profile_info profile_data = { 0, 0, 0 };
	fwrite(&profile_data, sizeof(profile_data), 1, profileData);
}

void objc_profile_write_symbols(char **symbols)
{
	if (NULL == profileData)
	{
		LOCK_RUNTIME_FOR_SCOPE();
		if (NULL == profileData)
		{
			profile_init();
		}
	}
	LOCK(&profileLock);
	while(*symbols)
	{
		char *address = *(symbols++);
		char *symbol = *(symbols++);
		fprintf(profileSymbols, "%zx %s\n", (size_t)address, symbol);
	}
	UNLOCK(&profileLock);
	fflush(profileSymbols);
}

/**
 * Profiling version of the slot lookup.  This takes a unique ID for the module
 * and the callsite as extra arguments.  The type of the receiver and the
 * address of the resulting function are then logged to a file.  These can then
 * be used to determine whether adding slot caching is worthwhile, and whether
 * any of the resulting methods should be speculatively inlined.
 */
void objc_msg_profile(id receiver, IMP method,
                      const char *module, int32_t callsite)
{
	// Initialize the logging lazily.  This prevents us from wasting any memory
	// when we are not profiling.
	if (NULL == profileData)
	{
		LOCK_RUNTIME_FOR_SCOPE();
		if (NULL == profileData)
		{
			profile_init();
		}
	}
	struct profile_info profile_data = { module, callsite, method };
	fwrite(&profile_data, sizeof(profile_data), 1, profileData);
}

/**
 * Looks up a slot without invoking any forwarding mechanisms
 */
Slot_t objc_get_slot(Class cls, SEL selector)
{
	Slot_t result = objc_dtable_lookup(cls->dtable, selector->index);
	if (0 == result)
	{
		void *dtable = dtable_for_class(cls);
		/* Install the dtable if it hasn't already been initialized. */
		if (dtable == uninstalled_dtable)
		{
			dtable = dtable_for_class(cls);
			result = objc_dtable_lookup(dtable, selector->index);
		}
		else
		{
			// Check again incase another thread updated the dtable while we
			// weren't looking
			result = objc_dtable_lookup(dtable, selector->index);
		}
		if (NULL == result)
		{
			if (!isSelRegistered(selector))
			{
				objc_register_selector(selector);
				return objc_get_slot(cls, selector);
			}
			if ((result = objc_dtable_lookup(dtable, get_untyped_idx(selector))))
			{
				return _objc_selector_type_mismatch(cls, selector, result);
			}
		}
	}
	return result;
}

////////////////////////////////////////////////////////////////////////////////
// Public API
////////////////////////////////////////////////////////////////////////////////

BOOL class_respondsToSelector(Class cls, SEL selector)
{
	if (0 == selector || 0 == cls) { return NO; }

	return NULL != objc_get_slot(cls, selector);
}

IMP class_getMethodImplementation(Class cls, SEL name)
{
	if ((Nil == cls) || (NULL == name)) { return (IMP)0; }
	Slot_t slot = objc_get_slot(cls, name);
	return NULL != slot ? slot->method : __objc_msg_forward2(nil, name);
}

IMP class_getMethodImplementation_stret(Class cls, SEL name)
{
	return class_getMethodImplementation(cls, name);
}


////////////////////////////////////////////////////////////////////////////////
// Legacy compatibility
////////////////////////////////////////////////////////////////////////////////

#ifndef NO_LEGACY
/**
 * Legacy message lookup function.
 */
BOOL __objc_responds_to(id object, SEL sel)
{
	return class_respondsToSelector(classForObject(object), sel);
}

IMP get_imp(Class cls, SEL selector)
{
	return class_getMethodImplementation(cls, selector);
}

/**
 * Message send function that only ever worked on a small subset of compiler /
 * architecture combinations.
 */
void *objc_msg_sendv(void)
{
	fprintf(stderr, "objc_msg_sendv() never worked correctly.  Don't use it.\n");
	abort();
}
#endif
/**
 * Legacy message lookup function.  Does not support fast proxies or safe IMP
 * caching.
 */
IMP objc_msg_lookup(id receiver, SEL selector)
{
	if (nil == receiver) { return (IMP)nil_method; }

	id self = receiver;
	Slot_t slot = objc_msg_lookup_internal(&self, selector, nil);
	if (self != receiver)
	{
		slot = __objc_msg_forward3(receiver, selector);
	}
	return slot->method;
}

IMP objc_msg_lookup_super(struct objc_super *super, SEL selector)
{
	return objc_slot_lookup_super(super, selector)->method;
}
