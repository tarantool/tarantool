#include <stdlib.h>
#include <assert.h>
#import "stdio.h"
#import "objc/runtime.h"
#import "objc/blocks_runtime.h"
#import "nsobject.h"
#import "class.h"
#import "selector.h"
#import "visibility.h"
#import "objc/hooks.h"
#import "objc/objc-arc.h"
#import "objc/blocks_runtime.h"

#ifndef NO_PTHREADS
#include <pthread.h>
pthread_key_t ARCThreadKey;
#endif

extern void _NSConcreteMallocBlock;
extern void _NSConcreteGlobalBlock;

@interface NSAutoreleasePool
+ (Class)class;
+ (id)new;
- (void)release;
@end

#define POOL_SIZE (4096 / sizeof(void*) - (2 * sizeof(void*)))
/**
 * Structure used for ARC-managed autorelease pools.  This structure should be
 * exactly one page in size, so that it can be quickly allocated.  This does
 * not correspond directly to an autorelease pool.  The 'pool' returned by
 * objc_autoreleasePoolPush() may be an interior pointer to one of these
 * structures.
 */
struct arc_autorelease_pool
{
	/**
	 * Pointer to the previous autorelease pool structure in the chain.  Set
	 * when pushing a new structure on the stack, popped during cleanup.
	 */
	struct arc_autorelease_pool *previous;
	/**
	 * The current insert point.
	 */
	id *insert;
	/**
	 * The remainder of the page, an array of object pointers.  
	 */
	id pool[POOL_SIZE];
};

struct arc_tls
{
	struct arc_autorelease_pool *pool;
	id returnRetained;
};

static inline struct arc_tls* getARCThreadData(void)
{
#ifdef NO_PTHREADS
	return NULL;
#else
	struct arc_tls *tls = pthread_getspecific(ARCThreadKey);
	if (NULL == tls)
	{
		tls = calloc(sizeof(struct arc_tls), 1);
		pthread_setspecific(ARCThreadKey, tls);
	}
	return tls;
#endif
}
int count = 0;
int poolCount = 0;
static inline void release(id obj);

/**
 * Empties objects from the autorelease pool, stating at the head of the list
 * specified by pool and continuing until it reaches the stop point.  If the stop point is NULL then 
 */
static void emptyPool(struct arc_tls *tls, id *stop)
{
	struct arc_autorelease_pool *stopPool = NULL;
	if (NULL != stop)
	{
		stopPool = tls->pool;
		while (1)
		{
			// Invalid stop location
			if (NULL == stopPool)
			{
				return;
			}
			// NULL is the placeholder for the top-level pool
			if (NULL == stop && stopPool->previous == NULL)
			{
				break;
			}
			// Stop location was found in this pool
			if ((stop >= stopPool->pool) && (stop < &stopPool->pool[POOL_SIZE]))
			{
				break;
			}
			stopPool = stopPool->previous;
		}
	}
	while (tls->pool != stopPool)
	{
		while (tls->pool->insert > tls->pool->pool)
		{
			tls->pool->insert--;
			// This may autorelease some other objects, so we have to work in
			// the case where the autorelease pool is extended during a -release.
			release(*tls->pool->insert);
			count--;
		}
		void *old = tls->pool;
		tls->pool = tls->pool->previous;
		free(old);
	}
	if (NULL != tls->pool)
	{
		while ((stop == NULL || (tls->pool->insert > stop)) &&
		       (tls->pool->insert > tls->pool->pool))
		{
			tls->pool->insert--;
			count--;
			release(*tls->pool->insert);
		}
	}
	//fprintf(stderr, "New insert: %p.  Stop: %p\n", tls->pool->insert, stop);
}

static void cleanupPools(struct arc_tls* tls)
{
	struct arc_autorelease_pool *pool = tls->pool;
	if (tls->returnRetained)
	{
		release(tls->returnRetained);
		tls->returnRetained = nil;
	}
	while(NULL != pool)
	{
		emptyPool(tls, NULL);
		assert(NULL == tls->pool);
	}
	if (tls->returnRetained)
	{
		cleanupPools(tls);
	}
	free(tls);
}


static Class AutoreleasePool;
static IMP NewAutoreleasePool;
static IMP DeleteAutoreleasePool;
static IMP AutoreleaseAdd;

extern BOOL FastARCRetain;
extern BOOL FastARCRelease;
extern BOOL FastARCAutorelease;

static BOOL useARCAutoreleasePool;

static inline id retain(id obj)
{
	if (isSmallObject(obj)) { return obj; }
	Class cls = obj->isa;
	if ((Class)&_NSConcreteMallocBlock == cls)
	{
		return Block_copy(obj);
	}
	if (objc_test_class_flag(cls, objc_class_flag_fast_arc))
	{
		intptr_t *refCount = ((intptr_t*)obj) - 1;
		__sync_add_and_fetch(refCount, 1);
		return obj;
	}
	return [obj retain];
}

static inline void release(id obj)
{
	if (isSmallObject(obj)) { return; }
	Class cls = obj->isa;
	if (objc_test_class_flag(cls, objc_class_flag_fast_arc))
	{
		intptr_t *refCount = ((intptr_t*)obj) - 1;
		if (__sync_sub_and_fetch(refCount, 1) < 0)
		{
			objc_delete_weak_refs(obj);
			[obj dealloc];
		}
		return;
	}
	[obj release];
}

static inline void initAutorelease(void)
{
	if (Nil == AutoreleasePool)
	{
		AutoreleasePool = objc_getRequiredClass("NSAutoreleasePool");
		if (Nil == AutoreleasePool)
		{
			useARCAutoreleasePool = YES;
		}
		else
		{
			[AutoreleasePool class];
			useARCAutoreleasePool = class_respondsToSelector(AutoreleasePool,
			                                                 SELECTOR(_ARCCompatibleAutoreleasePool));
			NewAutoreleasePool = class_getMethodImplementation(object_getClass(AutoreleasePool),
			                                                   SELECTOR(new));
			DeleteAutoreleasePool = class_getMethodImplementation(AutoreleasePool,
			                                                      SELECTOR(release));
			AutoreleaseAdd = class_getMethodImplementation(object_getClass(AutoreleasePool),
			                                               SELECTOR(addObject:));
		}
	}
}

static inline id autorelease(id obj)
{
	//fprintf(stderr, "Autoreleasing %p\n", obj);
	if (useARCAutoreleasePool)
	{
		struct arc_tls *tls = getARCThreadData();
		if (NULL != tls)
		{
			struct arc_autorelease_pool *pool = tls->pool;
			if (NULL == pool || (pool->insert >= &pool->pool[POOL_SIZE]))
			{
				pool = calloc(sizeof(struct arc_autorelease_pool), 1);
				pool->previous = tls->pool;
				pool->insert = pool->pool;
				tls->pool = pool;
			}
			count++;
			*pool->insert = obj;
			pool->insert++;
			return obj;
		}
	}
	if (objc_test_class_flag(classForObject(obj), objc_class_flag_fast_arc))
	{
		initAutorelease();
		if (0 != AutoreleaseAdd)
		{
			AutoreleaseAdd(AutoreleasePool, SELECTOR(addObject:), obj);
		}
		return obj;
	}
	return [obj autorelease];
}

unsigned long objc_arc_autorelease_count_np(void)
{
	struct arc_tls* tls = getARCThreadData();
	unsigned long count = 0;
	if (!tls) { return 0; }

	for (struct arc_autorelease_pool *pool=tls->pool ;
	     NULL != pool ;
	     pool = pool->previous)
	{
		count += (((intptr_t)pool->insert) - ((intptr_t)pool->pool)) / sizeof(id);
	}
	return count;
}
unsigned long objc_arc_autorelease_count_for_object_np(id obj)
{
	struct arc_tls* tls = getARCThreadData();
	unsigned long count = 0;
	if (!tls) { return 0; }

	for (struct arc_autorelease_pool *pool=tls->pool ;
	     NULL != pool ;
	     pool = pool->previous)
	{
		for (id* o = pool->insert-1 ; o >= pool->pool ; o--)
		{
			if (*o == obj)
			{
				count++;
			}
		}
	}
	return count;
}


void *objc_autoreleasePoolPush(void)
{
	initAutorelease();
	struct arc_tls* tls = getARCThreadData();
	// If there is an object in the return-retained slot, then we need to
	// promote it to the real autorelease pool BEFORE pushing the new
	// autorelease pool.  If we don't, then it may be prematurely autoreleased.
	if ((NULL != tls) && (nil != tls->returnRetained))
	{
		autorelease(tls->returnRetained);
		tls->returnRetained = nil;
	}
	if (useARCAutoreleasePool)
	{
		if (NULL != tls)
		{
			struct arc_autorelease_pool *pool = tls->pool;
			if (NULL == pool || (pool->insert >= &pool->pool[POOL_SIZE]))
			{
				pool = calloc(sizeof(struct arc_autorelease_pool), 1);
				pool->previous = tls->pool;
				pool->insert = pool->pool;
				tls->pool = pool;
			}
			// If there is no autorelease pool allocated for this thread, then
			// we lazily allocate one the first time something is autoreleased.
			return (NULL != tls->pool) ? tls->pool->insert : NULL;
		}
	}
	initAutorelease();
	if (0 == NewAutoreleasePool) { return NULL; }
	return NewAutoreleasePool(AutoreleasePool, SELECTOR(new));
}
void objc_autoreleasePoolPop(void *pool)
{
	if (useARCAutoreleasePool)
	{
		struct arc_tls* tls = getARCThreadData();
		if (NULL != tls)
		{
			if (NULL != tls->pool)
			{
				emptyPool(tls, pool);
			}
			return;
		}
	}
	DeleteAutoreleasePool(pool, SELECTOR(release));
	struct arc_tls* tls = getARCThreadData();
	if (tls && tls->returnRetained)
	{
		release(tls->returnRetained);
		tls->returnRetained = nil;
	}
}

id objc_autorelease(id obj)
{
	if (nil != obj)
	{
		obj = autorelease(obj);
	}
	return obj;
}

id objc_autoreleaseReturnValue(id obj)
{
	if (!useARCAutoreleasePool) 
	{
		struct arc_tls* tls = getARCThreadData();
		if (NULL != tls)
		{
			objc_autorelease(tls->returnRetained);
			tls->returnRetained = obj;
			return obj;
		}
	}
	return objc_autorelease(obj);
}

id objc_retainAutoreleasedReturnValue(id obj)
{
	// If the previous object was released  with objc_autoreleaseReturnValue()
	// just before return, then it will not have actually been autoreleased.
	// Instead, it will have been stored in TLS.  We just remove it from TLS
	// and undo the fake autorelease.
	//
	// If the object was not returned with objc_autoreleaseReturnValue() then
	// we actually autorelease the fake object. and then retain the argument.
	// In tis case, this is equivalent to objc_retain().
	struct arc_tls* tls = getARCThreadData();
	if (NULL != tls)
	{
		// If we're using our own autorelease pool, just pop the object from the top
		if (useARCAutoreleasePool)
		{
			if ((NULL != tls->pool) &&
			    (*(tls->pool->insert-1) == obj))
			{
				tls->pool->insert--;
				return obj;
			}
		}
		else if (obj == tls->returnRetained)
		{
			tls->returnRetained = NULL;
			return obj;
		}
	}
	return objc_retain(obj);
}

id objc_retain(id obj)
{
	if (nil == obj) { return nil; }
	return retain(obj);
}

id objc_retainAutorelease(id obj)
{
	return objc_autorelease(objc_retain(obj));
}

id objc_retainAutoreleaseReturnValue(id obj)
{
	if (nil == obj) { return obj; }
	return objc_autoreleaseReturnValue(retain(obj));
}


id objc_retainBlock(id b)
{
	return _Block_copy(b);
}

void objc_release(id obj)
{
	if (nil == obj) { return; }
	release(obj);
}

id objc_storeStrong(id *addr, id value)
{
	value = objc_retain(value);
	id oldValue = *addr;
	*addr = value;
	objc_release(oldValue);
	return value;
}

////////////////////////////////////////////////////////////////////////////////
// Weak references
////////////////////////////////////////////////////////////////////////////////

typedef struct objc_weak_ref
{
	id obj;
	id *ref[4];
	struct objc_weak_ref *next;
} WeakRef;


static int weak_ref_compare(const id obj, const WeakRef weak_ref)
{
	return obj == weak_ref.obj;
}

static uint32_t ptr_hash(const void *ptr)
{
	// Bit-rotate right 4, since the lowest few bits in an object pointer will
	// always be 0, which is not so useful for a hash value
	return ((uintptr_t)ptr >> 4) | ((uintptr_t)ptr << ((sizeof(id) * 8) - 4));
}
static int weak_ref_hash(const WeakRef weak_ref)
{
	return ptr_hash(weak_ref.obj);
}
static int weak_ref_is_null(const WeakRef weak_ref)
{
	return weak_ref.obj == NULL;
}
const static WeakRef NullWeakRef;
#define MAP_TABLE_NAME weak_ref
#define MAP_TABLE_COMPARE_FUNCTION weak_ref_compare
#define MAP_TABLE_HASH_KEY ptr_hash
#define MAP_TABLE_HASH_VALUE weak_ref_hash
#define MAP_TABLE_HASH_VALUE weak_ref_hash
#define MAP_TABLE_VALUE_TYPE struct objc_weak_ref
#define MAP_TABLE_VALUE_NULL weak_ref_is_null
#define MAP_TABLE_VALUE_PLACEHOLDER NullWeakRef
#define MAP_TABLE_ACCESS_BY_REFERENCE 1
#define MAP_TABLE_SINGLE_THREAD 1
#define MAP_TABLE_NO_LOCK 1

#include "hash_table.h"

static weak_ref_table *weakRefs;
mutex_t weakRefLock;

PRIVATE void init_arc(void)
{
	weak_ref_initialize(&weakRefs, 128);
	INIT_LOCK(weakRefLock);
#ifndef NO_PTHREADS
	pthread_key_create(&ARCThreadKey, (void(*)(void*))cleanupPools);
#endif
}

void* block_load_weak(void *block);

id objc_storeWeak(id *addr, id obj)
{
	id old = *addr;
	LOCK_FOR_SCOPE(&weakRefLock);
	if (nil != old)
	{
		WeakRef *oldRef = weak_ref_table_get(weakRefs, old);
		while (NULL != oldRef)
		{
			for (int i=0 ; i<4 ; i++)
			{
				if (oldRef->ref[i] == addr)
				{
					oldRef->ref[i] = 0;
					oldRef = 0;
					break;
				}
			}
			oldRef = oldRef->next;
		}
	}
	if (nil == obj)
	{
		*addr = obj;
		return nil;
	}
	Class cls = classForObject(obj);
	if (&_NSConcreteGlobalBlock == cls)
	{
		// If this is a global block, it's never deallocated, so secretly make
		// this a strong reference
		// TODO: We probably also want to do the same for constant strings and
		// classes.
		*addr = obj;
		return obj;
	}
	if (&_NSConcreteMallocBlock == cls)
	{
		obj = block_load_weak(obj);
	}
	else if (objc_test_class_flag(cls, objc_class_flag_fast_arc))
	{
		if ((*(((intptr_t*)obj) - 1)) < 0)
		{
			return nil;
		}
	}
	else
	{
		obj = _objc_weak_load(obj);
	}
	if (nil != obj)
	{
		WeakRef *ref = weak_ref_table_get(weakRefs, obj);
		while (NULL != ref)
		{
			for (int i=0 ; i<4 ; i++)
			{
				if (0 == ref->ref[i])
				{
					ref->ref[i] = addr;
					*addr = obj;
					return obj;
				}
			}
			if (ref->next == NULL)
			{
				break;
			}
			ref = ref->next;
		}
		if (NULL != ref)
		{
			ref->next = calloc(sizeof(WeakRef), 1);
			ref->next->ref[0] = addr;
		}
		else
		{
			WeakRef newRef = {0};
			newRef.obj = obj;
			newRef.ref[0] = addr;
			weak_ref_insert(weakRefs, newRef);
		}
	}
	*addr = obj;
	return obj;
}

static void zeroRefs(WeakRef *ref, BOOL shouldFree)
{
	if (NULL != ref->next)
	{
		zeroRefs(ref->next, YES);
	}
	for (int i=0 ; i<4 ; i++)
	{
		if (0 != ref->ref[i])
		{
			*ref->ref[i] = 0;
		}
	}
	if (shouldFree)
	{
		free(ref);
	}
	else
	{
		memset(ref, 0, sizeof(WeakRef));
	}
}

void objc_delete_weak_refs(id obj)
{
	LOCK_FOR_SCOPE(&weakRefLock);
	WeakRef *oldRef = weak_ref_table_get(weakRefs, obj);
	if (0 != oldRef)
	{
		zeroRefs(oldRef, NO);
	}
}

id objc_loadWeakRetained(id* addr)
{
	LOCK_FOR_SCOPE(&weakRefLock);
	id obj = *addr;
	if (nil == obj) { return nil; }
	Class cls = classForObject(obj);
	if (&_NSConcreteMallocBlock == cls)
	{
		obj = block_load_weak(obj);
	}
	else if (objc_test_class_flag(cls, objc_class_flag_fast_arc))
	{
		if ((*(((intptr_t*)obj) - 1)) < 0)
		{
			return nil;
		}
	}
	else
	{
		obj = _objc_weak_load(obj);
	}
	return objc_retain(obj);
}

id objc_loadWeak(id* object)
{
	return objc_autorelease(objc_loadWeakRetained(object));
}

void objc_copyWeak(id *dest, id *src)
{
	objc_release(objc_initWeak(dest, objc_loadWeakRetained(src)));
}

void objc_moveWeak(id *dest, id *src)
{
	// Don't retain or release.  While the weak ref lock is held, we know that
	// the object can't be deallocated, so we just move the value and update
	// the weak reference table entry to indicate the new address.
	LOCK_FOR_SCOPE(&weakRefLock);
	*dest = *src;
	*src = nil;
	WeakRef *oldRef = weak_ref_table_get(weakRefs, *dest);
	while (NULL != oldRef)
	{
		for (int i=0 ; i<4 ; i++)
		{
			if (oldRef->ref[i] == src)
			{
				oldRef->ref[i] = dest;
				return;
			}
		}
	}
}

void objc_destroyWeak(id* obj)
{
	objc_storeWeak(obj, nil);
}

id objc_initWeak(id *object, id value)
{
	*object = nil;
	return objc_storeWeak(object, value);
}
