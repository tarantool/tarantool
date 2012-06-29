#include "objc/runtime.h"
#include "protocol.h"
#include "properties.h"
#include "class.h"
#include "lock.h"
#include <stdlib.h>

#define BUFFER_TYPE struct objc_protocol_list
#include "buffer.h"

// Get the functions for string hashing
#include "string_hash.h"

static int protocol_compare(const char *name, 
                            const struct objc_protocol2 *protocol)
{
	return string_compare(name, protocol->name);
}
static int protocol_hash(const struct objc_protocol2 *protocol)
{
	return string_hash(protocol->name);
}
#define MAP_TABLE_NAME protocol
#define MAP_TABLE_COMPARE_FUNCTION protocol_compare
#define MAP_TABLE_HASH_KEY string_hash
#define MAP_TABLE_HASH_VALUE protocol_hash
#include "hash_table.h"

static protocol_table *known_protocol_table;

void init_protocol_table(void)
{
	protocol_initialize(&known_protocol_table, 128);
}  

static void protocol_table_insert(const struct objc_protocol2 *protocol)
{
	protocol_insert(known_protocol_table, (void*)protocol);
}

struct objc_protocol2 *protocol_for_name(const char *name)
{
	return protocol_table_get(known_protocol_table, name);
}

static id ObjC2ProtocolClass = 0;

static int isEmptyProtocol(struct objc_protocol2 *aProto)
{
	int isEmpty = 
		((aProto->instance_methods == NULL) || 
			(aProto->instance_methods->count == 0)) &&
		((aProto->class_methods == NULL) || 
			(aProto->class_methods->count == 0)) &&
		((aProto->protocol_list == NULL) ||
		  (aProto->protocol_list->count == 0));
	if (aProto->isa == ObjC2ProtocolClass)
	{
		struct objc_protocol2 *p2 = (struct objc_protocol2*)aProto;
		isEmpty &= (p2->optional_instance_methods->count == 0);
		isEmpty &= (p2->optional_class_methods->count == 0);
		isEmpty &= (p2->properties == 0) || (p2->properties->count == 0);
		isEmpty &= (p2->optional_properties == 0) || (p2->optional_properties->count == 0);
	}
	return isEmpty;
}

// FIXME: Make p1 adopt all of the stuff in p2
static void makeProtocolEqualToProtocol(struct objc_protocol2 *p1,
                                        struct objc_protocol2 *p2) 
{
#define COPY(x) p1->x = p2->x
	COPY(instance_methods);
	COPY(class_methods);
	COPY(protocol_list);
	if (p1->isa == ObjC2ProtocolClass &&
		p2->isa == ObjC2ProtocolClass)
	{
		COPY(optional_instance_methods);
		COPY(optional_class_methods);
		COPY(properties);
		COPY(optional_properties);
	}
#undef COPY
}

static struct objc_protocol2 *unique_protocol(struct objc_protocol2 *aProto)
{
	if (ObjC2ProtocolClass == 0)
	{
		ObjC2ProtocolClass = objc_getClass("Protocol2");
	}
	struct objc_protocol2 *oldProtocol = 
		protocol_for_name(aProto->name);
	if (NULL == oldProtocol)
	{
		// This is the first time we've seen this protocol, so add it to the
		// hash table and ignore it.
		protocol_table_insert(aProto);
		return aProto;
	}
	if (isEmptyProtocol(oldProtocol))
	{
		if (isEmptyProtocol(aProto))
		{
			return aProto;
			// Add protocol to a list somehow.
		}
		else
		{
			// This protocol is not empty, so we use its definitions
			makeProtocolEqualToProtocol(oldProtocol, aProto);
			return aProto;
		}
	}
	else
	{
		if (isEmptyProtocol(aProto))
		{
			makeProtocolEqualToProtocol(aProto, oldProtocol);
			return oldProtocol;
		}
		else
		{
			return oldProtocol;
			//FIXME: We should really perform a check here to make sure the
			//protocols are actually the same.
		}
	}
}

static id protocol_class;
static id protocol_class2;
enum protocol_version
{
	/**
	 * Legacy (GCC-compatible) protocol version.
	 */
	protocol_version_legacy = 2,
	/**
	 * New (Objective-C 2-compatible) protocol version.
	 */
	protocol_version_objc2 = 3
};

static BOOL init_protocols(struct objc_protocol_list *protocols)
{
	// Protocol2 is a subclass of Protocol, so if we have loaded Protocol2 we
	// must have also loaded Protocol.
	if (nil == protocol_class2)
	{
		protocol_class = objc_getClass("Protocol");
		protocol_class2 = objc_getClass("Protocol2");
	}
	if (nil == protocol_class2 || nil == protocol_class)
	{
		return NO;
	}

	for (unsigned i=0 ; i<protocols->count ; i++)
	{
		struct objc_protocol2 *aProto = protocols->list[i];
		// Don't initialise a protocol twice
		if (aProto->isa == protocol_class ||
			aProto->isa == protocol_class2) { continue ;}

		// Protocols in the protocol list have their class pointers set to the
		// version of the protocol class that they expect.
		enum protocol_version version = 
			(enum protocol_version)(uintptr_t)aProto->isa;
		switch (version)
		{
			default:
				fprintf(stderr, "Unknown protocol version");
				abort();
			case protocol_version_legacy:
				aProto->isa = protocol_class;
				break;
			case protocol_version_objc2:
				aProto->isa = protocol_class2;
				break;
		}
		// Initialize all of the protocols that this protocol refers to
		if (NULL != aProto->protocol_list)
		{
			init_protocols(aProto->protocol_list);
		}
		// Replace this protocol with a unique version of it.
		protocols->list[i] = unique_protocol(aProto);
	}
	return YES;
}

PRIVATE void objc_init_protocols(struct objc_protocol_list *protocols)
{
	if (!init_protocols(protocols))
	{
		set_buffered_object_at_index(protocols, buffered_objects++);
		return;
	}
	if (buffered_objects > 0) { return; }

	// If we can load one protocol, then we can load all of them.
	for (unsigned i=0 ; i<buffered_objects ; i++)
	{
		struct objc_protocol_list *c = buffered_object_at_index(i);
		if (NULL != c)
		{
			init_protocols(c);
			set_buffered_object_at_index(NULL, i);
		}
	}
	compact_buffer();
}

// Public functions:
Protocol *objc_getProtocol(const char *name)
{
	if (NULL == name) { return NULL; }
	return (Protocol*)protocol_for_name(name);
}

BOOL protocol_conformsToProtocol(Protocol *p1, Protocol *p2)
{
	if (NULL == p1 || NULL == p2) { return NO; }

	// A protocol trivially conforms to itself
	if (strcmp(p1->name, p2->name) == 0) { return YES; }

	for (struct objc_protocol_list *list = p1->protocol_list ;
		list != NULL ; list = list->next)
	{
		for (int i=0 ; i<list->count ; i++)
		{
			if (strcmp(list->list[i]->name, p2->name) == 0)
			{
				return YES;
			}
			if (protocol_conformsToProtocol((Protocol*)list->list[i], p2))
			{
				return YES;
			}
		}
	}
	return NO;
}

BOOL class_conformsToProtocol(Class cls, Protocol *protocol)
{
	if (Nil == cls || NULL == protocol) { return NO; }
	for ( ; Nil != cls ; cls = class_getSuperclass(cls))
	{
		for (struct objc_protocol_list *protocols = cls->protocols;
			protocols != NULL ; protocols = protocols->next)
		{
			for (int i=0 ; i<protocols->count ; i++)
			{
				Protocol *p1 = (Protocol*)protocols->list[i];
				if (protocol_conformsToProtocol(p1, protocol))
				{
					return YES;
				}
			}
		}
	}
	return NO;
}

static struct objc_method_description_list *
get_method_list(Protocol *p,
                BOOL isRequiredMethod,
                BOOL isInstanceMethod)
{
	static id protocol2 = NULL;

	if (NULL == protocol2)
	{
		protocol2 = objc_getClass("Protocol2");
	}
	struct objc_method_description_list *list;
	if (isRequiredMethod)
	{
		if (isInstanceMethod)
		{
			list = p->instance_methods;
		}
		else
		{
			list = p->class_methods;
		}
	}
	else
	{
		if (p->isa != protocol2) { return NULL; }


		if (isInstanceMethod)
		{
			list = ((Protocol2*)p)->optional_instance_methods;
		}
		else
		{
			list = ((Protocol2*)p)->optional_class_methods;
		}
	}
	return list;
}

struct objc_method_description *protocol_copyMethodDescriptionList(Protocol *p,
	BOOL isRequiredMethod, BOOL isInstanceMethod, unsigned int *count)
{
	if (NULL == p) { return NULL; }
	struct objc_method_description_list *list = 
		get_method_list(p, isRequiredMethod, isInstanceMethod);
	*count = 0;
	if (NULL == list || list->count == 0) { return NULL; }

	*count = list->count;
	struct objc_method_description *out = 
		calloc(sizeof(struct objc_method_description_list), list->count);

	for (int i=0 ; i<list->count ; i++)
	{
		out[i].name = sel_registerTypedName_np(list->methods[i].name,
		                                       list->methods[i].types);
		out[i].types = list->methods[i].types;
	}
	return out;
}

Protocol*__unsafe_unretained* protocol_copyProtocolList(Protocol *p, unsigned int *count)
{
	if (NULL == p) { return NULL; }
	*count = 0;
	if (p->protocol_list == NULL || p->protocol_list->count ==0)
	{
		return NULL;
	}

	Protocol **out = calloc(sizeof(Protocol*), p->protocol_list->count);
	for (int i=0 ; i<p->protocol_list->count ; i++)
	{
		out[i] = (Protocol*)p->protocol_list->list[i];
	}
	return NULL;
}

objc_property_t *protocol_copyPropertyList(Protocol *protocol,
                                           unsigned int *outCount)
{
	if (NULL == protocol) { return NULL; }
	if (protocol->isa != ObjC2ProtocolClass)
	{
		return NULL;
	}
	Protocol2 *p = (Protocol2*)protocol;
	struct objc_property_list *properties = p->properties;
	unsigned int count = 0;
	if (NULL != properties)
	{
		count = properties->count;
	}
	if (NULL != p->optional_properties)
	{
		count = p->optional_properties->count;
	}
	if (0 == count)
	{
		return NULL;
	}
	objc_property_t *list = calloc(sizeof(objc_property_t), count);
	unsigned int out = 0;
	if (properties)
	{
		for (int i=0 ; i<properties->count ; i++)
		{
			list[out] = &properties->properties[i];
		}
	}
	properties = p->optional_properties;
	if (properties)
	{
		for (int i=0 ; i<properties->count ; i++)
		{
			list[out] = &properties->properties[i];
		}
	}
	*outCount = count;
	return list;
}

objc_property_t protocol_getProperty(Protocol *protocol,
                                     const char *name,
                                     BOOL isRequiredProperty,
                                     BOOL isInstanceProperty)
{
	if (NULL == protocol) { return NULL; }
	// Class properties are not supported yet (there is no language syntax for
	// defining them!)
	if (!isInstanceProperty) { return NULL; }
	if (protocol->isa != ObjC2ProtocolClass)
	{
		return NULL;
	}
	Protocol2 *p = (Protocol2*)protocol;
	struct objc_property_list *properties = 
	    isRequiredProperty ? p->properties : p->optional_properties;
	while (NULL != properties)
	{
		for (int i=0 ; i<properties->count ; i++)
		{
			objc_property_t prop = &properties->properties[i];
			if (strcmp(prop->name, name) == 0)
			{
				return prop;
			}
		}
		properties = properties->next;
	}
	return NULL;
}


struct objc_method_description 
protocol_getMethodDescription(Protocol *p,
                              SEL aSel,
                              BOOL isRequiredMethod,
                              BOOL isInstanceMethod)
{
	struct objc_method_description d = {0,0};
	struct objc_method_description_list *list = 
		get_method_list(p, isRequiredMethod, isInstanceMethod);
	if (NULL == list)
	{
		return d;
	}
	// TODO: We could make this much more efficient if 
	for (int i=0 ; i<list->count ; i++)
	{
		SEL s = sel_registerTypedName_np(list->methods[i].name, 0);
		if (sel_isEqual(s, aSel))
		{
			d.name = s;
			d.types = list->methods[i].types;
			break;
		}
	}
	return d;
}


const char *protocol_getName(Protocol *p)
{
	if (NULL != p)
	{
		return p->name;
	}
	return NULL;
}

BOOL protocol_isEqual(Protocol *p, Protocol *other)
{
	if (NULL == p || NULL == other)
	{
		return NO;
	}
	if (p == other || 
		p->name == other->name ||
		0 == strcmp(p->name, other->name))
	{
		return YES;
	}
	return NO;
}

Protocol*__unsafe_unretained* objc_copyProtocolList(unsigned int *outCount)
{
	unsigned int total = known_protocol_table->table_used;
	Protocol **p = calloc(sizeof(Protocol*), known_protocol_table->table_used);

	struct protocol_table_enumerator *e = NULL;
	Protocol *next;

	unsigned int count = 0;
	while ((count < total) && (next = protocol_next(known_protocol_table, &e)))
	{
		p[count++] = next;
	}
	if (NULL != outCount)
	{
		*outCount = total;
	}
	return p;
}


Protocol *objc_allocateProtocol(const char *name)
{
	if (objc_getProtocol(name) != NULL) { return NULL; }
	Protocol *p = calloc(1, sizeof(Protocol2));
	p->name = strdup(name);
	return p;
}
void objc_registerProtocol(Protocol *proto)
{
	if (NULL == proto) { return; }
	LOCK_RUNTIME_FOR_SCOPE();
	if (objc_getProtocol(proto->name) != NULL) { return; }
	if (nil != proto->isa) { return; }
	proto->isa = ObjC2ProtocolClass;
	protocol_table_insert((struct objc_protocol2*)proto);
}
void protocol_addMethodDescription(Protocol *aProtocol,
                                   SEL name,
                                   const char *types,
                                   BOOL isRequiredMethod,
                                   BOOL isInstanceMethod)
{
	if ((NULL == aProtocol) || (NULL == name) || (NULL == types)) { return; }
	if (nil != aProtocol->isa) { return; }
	Protocol2 *proto = (Protocol2*)aProtocol;
	struct objc_method_description_list **listPtr;
	if (isInstanceMethod)
	{
		if (isRequiredMethod)
		{
			listPtr = &proto->instance_methods;
		}
		else
		{
			listPtr = &proto->optional_instance_methods;
		}
	}
	else
	{
		if (isRequiredMethod)
		{
			listPtr = &proto->class_methods;
		}
		else
		{
			listPtr = &proto->optional_class_methods;
		}
	}
	if (NULL == *listPtr)
	{
		*listPtr = calloc(1, sizeof(struct objc_method_description_list) + sizeof(struct objc_method_description));
		(*listPtr)->count = 1;
	}
	else
	{
		(*listPtr)->count++;
		*listPtr = realloc(*listPtr, sizeof(struct objc_method_description_list) +
				sizeof(struct objc_method_description) * (*listPtr)->count);
	}
	struct objc_method_description_list *list = *listPtr;
	int index = list->count-1;
	list->methods[index].name = sel_getName(name);
	list->methods[index].types= types;
}
void protocol_addProtocol(Protocol *aProtocol, Protocol *addition)
{
	if ((NULL == aProtocol) || (NULL == addition)) { return; }
	Protocol2 *proto = (Protocol2*)aProtocol;
	if (NULL == proto->protocol_list)
	{
		proto->protocol_list = calloc(1, sizeof(struct objc_property_list) + sizeof(Protocol2*));
		proto->protocol_list->count = 1;
	}
	else
	{
		proto->protocol_list->count++;
		proto->protocol_list = realloc(proto->protocol_list, sizeof(struct objc_property_list) +
				proto->protocol_list->count * sizeof(Protocol2*));
		proto->protocol_list->count = 1;
	}
	proto->protocol_list->list[proto->protocol_list->count-1] = (Protocol2*)addition;
}
void protocol_addProperty(Protocol *aProtocol,
                          const char *name,
                          const objc_property_attribute_t *attributes,
                          unsigned int attributeCount,
                          BOOL isRequiredProperty,
                          BOOL isInstanceProperty)
{
	if ((NULL == aProtocol) || (NULL == name)) { return; }
	if (nil != aProtocol->isa) { return; }
	if (!isInstanceProperty) { return; }
	Protocol2 *proto = (Protocol2*)aProtocol;
	struct objc_property_list **listPtr;
	if (isRequiredProperty)
	{
		listPtr = &proto->properties;
	}
	else
	{
		listPtr = &proto->optional_properties;
	}
	if (NULL == *listPtr)
	{
		*listPtr = calloc(1, sizeof(struct objc_property_list) + sizeof(struct objc_property));
		(*listPtr)->count = 1;
	}
	else
	{
		(*listPtr)->count++;
		*listPtr = realloc(*listPtr, sizeof(struct objc_property_list) +
				sizeof(struct objc_property) * (*listPtr)->count);
	}
	struct objc_property_list *list = *listPtr;
	int index = list->count-1;
	struct objc_property p = propertyFromAttrs(attributes, attributeCount);
	p.name = strdup(name);
	memcpy(&(list->properties[index]), &p, sizeof(p));
}

