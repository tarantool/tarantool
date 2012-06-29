#include <stdlib.h>
#include <assert.h>
#include "objc/runtime.h"
#include "lock.h"
#include "loader.h"
#include "visibility.h"
#ifdef ENABLE_GC
#include <gc/gc.h>
#endif
#include <stdio.h>

/**
 * Runtime lock.  This is exposed in 
 */
PRIVATE mutex_t runtime_mutex;
LEGACY void *__objc_runtime_mutex = &runtime_mutex;

void init_alias_table(void);
void init_arc(void);
void init_class_tables(void);
void init_dispatch_tables(void);
void init_gc(void);
void init_protocol_table(void);
void init_selector_tables(void);
void init_trampolines(void);
void objc_send_load_message(Class class);

/* Number of threads that are alive.  */
int __objc_runtime_threads_alive = 1;			/* !T:MUTEX */

void __objc_exec_class(struct objc_module_abi_8 *module)
{
	static BOOL first_run = YES;

	// Check that this module uses an ABI version that we recognise.  
	// In future, we should pass the ABI version to the class / category load
	// functions so that we can change various structures more easily.
	assert(objc_check_abi_version(module));

	if (first_run)
	{
#if ENABLE_GC
		init_gc();
#endif
		// Create the main runtime lock.  This is not safe in theory, but in
		// practice the first time that this function is called will be in the
		// loader, from the main thread.  Future loaders may run concurrently,
		// but that is likely to break the semantics of a lot of languages, so
		// we don't have to worry about it for a long time.
		//
		// The only case when this can potentially go badly wrong is when a
		// pure-C main() function spawns two threads which then, concurrently,
		// call dlopen() or equivalent, and the platform's implementation of
		// this does not perform any synchronization.
		INIT_LOCK(runtime_mutex);
		// Create the various tables that the runtime needs.
		init_selector_tables();
		init_protocol_table();
		init_class_tables();
		init_dispatch_tables();
		init_alias_table();
		init_arc();
		init_trampolines();
		first_run = NO;
	}

	// The runtime mutex is held for the entire duration of a load.  It does
	// not need to be acquired or released in any of the called load functions.
	LOCK_RUNTIME_FOR_SCOPE();

	struct objc_symbol_table_abi_8 *symbols = module->symbol_table;
	// Register all of the selectors used in this module.
	if (symbols->selectors)
	{
		objc_register_selector_array(symbols->selectors,
				symbols->selector_count);
	}

	unsigned short defs = 0;
	// Load the classes from this module
	for (unsigned short i=0 ; i<symbols->class_count ; i++)
	{
		objc_load_class(symbols->definitions[defs++]);
	}
	unsigned int category_start = defs;
	// Load the categories from this module
	for (unsigned short i=0 ; i<symbols->category_count; i++)
	{
		objc_try_load_category(symbols->definitions[defs++]);
	}
	// Load the static instances
	struct objc_static_instance_list **statics = (void*)symbols->definitions[defs];
	while (NULL != statics && NULL != *statics)
	{
		objc_init_statics(*(statics++));
	}

	// Load categories and statics that were deferred.
	objc_load_buffered_categories();
	objc_init_buffered_statics();
	// Fix up the class links for loaded classes.
	objc_resolve_class_links();
	for (unsigned short i=0 ; i<symbols->category_count; i++)
	{
		struct objc_category *cat = (struct objc_category*)
			symbols->definitions[category_start++];
		Class class = (Class)objc_getClass(cat->class_name);
		if ((Nil != class) && 
		    objc_test_class_flag(class, objc_class_flag_resolved))
		{
			objc_send_load_message(class);
		}
	}
}
