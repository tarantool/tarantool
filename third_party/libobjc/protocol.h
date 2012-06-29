#include "selector.h"
#include <stdlib.h>

struct objc_method_description_list
{
	/** 
	 * Number of method descriptions in this list.
	 */
	int count;
	/**
	 * Methods in this list.  Note: these selectors are NOT resolved.  The name
	 * field points to the name, not to the index of the uniqued version of the
	 * name.  You must not use them for dispatch.
	 */
	struct objc_selector methods[];
};


#ifdef __OBJC__
@interface Object { id isa; } @end
/**
 * Definition of the Protocol type.  Protocols are objects, but are rarely used
 * as such.
 */
@interface Protocol : Object
{
	@public
#else
struct objc_protocol
{
	/** Class pointer. */
	id                                   isa;
#endif 
	/** 
	 * The name of this protocol.  Two protocols are regarded as identical if
	 * they have the same name. 
	 */
	char                                *name;
	/**
	 * The list of protocols that this protocol conforms to.
	 */
	struct objc_protocol_list           *protocol_list;
	/**
	 * List of instance methods required by this protocol.
	 */
	struct objc_method_description_list *instance_methods;
	/**
	 * List of class methods required by this protocol.
	 */
	struct objc_method_description_list *class_methods; 
}
#ifdef __OBJC__
@end
#else
;
#endif 

#ifdef __OBJC__
@interface Protocol2 : Protocol
{
	@public
#else
typedef struct objc_protocol2
{
	/**
	 * Redefinition of the superclass ivars in the C version.
	 */
	id                                   isa;
	char                                *name;
	struct objc_protocol_list           *protocol_list;
	struct objc_method_description_list *instance_methods;
	struct objc_method_description_list *class_methods; 
#endif 
	/**
	 * Instance methods that are declared as optional for this protocol.
	 */
	struct objc_method_description_list *optional_instance_methods;
	/**
	 * Class methods that are declared as optional for this protocol.
	 */
	struct objc_method_description_list *optional_class_methods; 
	/**
	 * Properties that are required by this protocol.
	 */
	struct objc_property_list           *properties;
	/**
	 * Optional properties. 
	 */
	struct objc_property_list           *optional_properties;
}
#ifdef __OBJC__
@end
#else
Protocol2;
#endif

/**
 * List of protocols.  Attached to a class or a category by the compiler and to
 * a class by the runtime.
 */
struct objc_protocol_list
{
	/**
	 * Additional protocol lists.  Loading a category that declares protocols
	 * will cause a new list to be prepended using this pointer to the protocol
	 * list for the class.  Unlike methods, protocols can not be overridden,
	 * although it is possible for a protocol to appear twice.
	 */
	struct objc_protocol_list *next;
	/**
	 * The number of protocols in this list.
	 */
	size_t                     count;
	/**
	 * An array of protocols.  Actually contains count elements, not 1.
	 *
	 * The instances in this array may be any version of protocols.
	 */
	Protocol2                 *list[];
};

