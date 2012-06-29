#include <stdio.h>
#include <stdlib.h>
#include "objc/runtime.h"
#include "class.h"
#include "ivar.h"
#include "visibility.h"

ptrdiff_t objc_alignof_type(const char *);
ptrdiff_t objc_sizeof_type(const char *);

PRIVATE void objc_compute_ivar_offsets(Class class)
{
	int i = 0;
	/* If this class was compiled with support for late-bound ivars, the
	* instance_size field will contain 0 - {the size of the instance variables
	* declared for just this class}.  The individual instance variable offset
	* fields will then be the offsets from the start of the class, and so must
	* have the size of the parent class prepended. */
	if (class->instance_size <= 0)
	{
		Class super = class_getSuperclass(class);
		long ivar_start = 0;
		if (Nil != super)
		{
			if (super->instance_size <= 0)
			{
				objc_compute_ivar_offsets(super);
			}
			ivar_start = super->instance_size;
		}
		class->instance_size = ivar_start - class->instance_size;
		/* For each instance variable, we add the offset if required (it will be zero
		* if this class is compiled with a static ivar layout).  We then set the
		* value of a global variable to the offset value.  
		*
		* Any class compiled with support for the non-fragile ABI, but not actually
		* using it, will export the ivar offset field as a symbol.
		*
		* Note that using non-fragile ivars breaks @defs().  If you need equivalent
		* functionality, provide an alternative @interface with all variables
		* declared @public.
		*/
		if (class->ivars)
		{
			for (i = 0 ; i < class->ivars->count ; i++)
			{
				struct objc_ivar *ivar = &class->ivars->ivar_list[i];
				ivar->offset += ivar_start;
				/* If we're using the new ABI then we also set up the faster ivar
				* offset variables.
				*/
				if (objc_test_class_flag(class, objc_class_flag_new_abi))
				{
					*(class->ivar_offsets[i]) = ivar->offset;
				}
			}
		}
	}
	else
	{
		if (NULL == class->ivars) { return; }

		Class super = class_getSuperclass(class);
		int start = class->ivars->ivar_list[0].offset;
		/* Quick and dirty test.  If the first ivar comes straight after the last
		* class, then it's fine. */
		if (Nil == super || start == super->instance_size) {return; }

		/* Find the last superclass with at least one ivar. */
		while (NULL == super->ivars) 
		{
			super = class_getSuperclass(super);
		}
		struct objc_ivar *ivar =
			&super->ivars->ivar_list[super->ivars->count-1];

		// Find the end of the last ivar - instance_size contains some padding
		// for alignment.
		int real_end = ivar->offset + objc_sizeof_type(ivar->type);
		// Keep going if the new class starts at the end of the superclass
		if (start == real_end)
		{
			return;
		}
		// The classes don't line up, but don't panic; check that the
		// difference is not just padding for alignment
		int align = objc_alignof_type(class->ivars->ivar_list[0].type);
		if (start > real_end && (start - align) < real_end)
		{
			return;
		}

		/* Panic if this class has an instance variable that overlaps the
		* superclass. */
		fprintf(stderr, 
			"Error: Instance variables in %s overlap superclass %s.  ",
			class->name, super->name);
		fprintf(stderr, 
			"Offset of first instance variable, %s, is %d.  ",
			class->ivars->ivar_list[0].name, start);
		fprintf(stderr, 
			"Last instance variable in superclass, %s, ends at offset %d.  ",
			ivar->name, ivar->offset +
			(int)objc_sizeof_type(ivar->type));
		fprintf(stderr, "This probably means that you are subclassing a"
			"class from a library, which has changed in a binary-incompatible"
			"way.\n");
		abort();
	}
}

////////////////////////////////////////////////////////////////////////////////
// Public API functions
////////////////////////////////////////////////////////////////////////////////

void object_setIvar(id object, Ivar ivar, id value)
{
	char *addr = (char*)object;
	addr += ivar_getOffset(ivar);
	*(id*)addr = value;
}

Ivar object_setInstanceVariable(id obj, const char *name, void *value)
{
	Ivar ivar = class_getInstanceVariable(object_getClass(obj), name);
	object_setIvar(obj, ivar, value);
	return ivar;
}

id object_getIvar(id object, Ivar ivar)
{
	return *(id*)(((char*)object) + ivar_getOffset(ivar));
}

Ivar object_getInstanceVariable(id obj, const char *name, void **outValue)
{
	Ivar ivar = class_getInstanceVariable(object_getClass(obj), name);
	if (NULL != outValue)
	{
		*outValue = object_getIvar(obj, ivar);
	}
	return ivar;
}
