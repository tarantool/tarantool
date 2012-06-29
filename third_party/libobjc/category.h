
/**
 * The structure used to represent a category.
 *
 * This provides a set of new definitions that are used to replace those
 * contained within a class.
 *
 * Note: Objective-C 2 allows properties to be added to classes.  The current
 * ABI does not provide a field for adding properties in categories.  This is
 * likely to be added with ABI version 10.  Until then, the methods created by
 * a declared property will work, but introspection on the property will not.
 */
struct objc_category 
{
	/** 
	 * The name of this category.
	 */
	const char                *name;
	/**
	 * The name of the class to which this category should be applied.
	 */
	const char                *class_name;
	/**
	 * The list of instance methods to add to the class.
	 */
	struct objc_method_list   *instance_methods;
	/**
	 * The list of class methods to add to the class.
	 */
	struct objc_method_list   *class_methods;
	/**
	 * The list of protocols adopted by this category.
	 */
	struct objc_protocol_list *protocols;
};
