#ifndef ENABLE_GC
#include "objc/toydispatch.h"
#include "lock.h"
#include "visibility.h"


static dispatch_queue_t garbage_queue;

PRIVATE void objc_collect_garbage_data(void(*cleanup)(void*), void *garbage)
{
	if (0 == garbage_queue)
	{
		LOCK_RUNTIME_FOR_SCOPE();
		if (0 == garbage_queue)
		{
			garbage_queue = dispatch_queue_create("ObjC deferred free queue", 0);
		}
	}
	dispatch_async_f(garbage_queue, garbage, cleanup);
}

#endif
