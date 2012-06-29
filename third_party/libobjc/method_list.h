/**
 * Metadata structure describing a method.  
 */
struct objc_method
{
	/**
	 * Selector used to send messages to this method.  The type encoding of
	 * this method should match the types field.
	 */
	SEL         selector;
	/**
	 * The type encoding for this selector.  Used only for introspection, and
	 * only required because of the stupid selector handling in the old GNU
	 * runtime.  In future, this field may be reused for something else.
	 */
	const char *types;
	/**
	 * A pointer to the function implementing this method.
	 */
	IMP         imp;
};

/**
 * Method list.  Each class or category defines a new one of these and they are
 * all chained together in a linked list, with new ones inserted at the head.
 * When constructing the dispatch table, methods in the start of the list are
 * used in preference to ones at the end.
 */
struct objc_method_list
{
	/**
	 * The next group of methods in the list.
	 */
	struct objc_method_list  *next;
	/**
	 * The number of methods in this list.
	 */
	int                       count;
	/**
	 * An array of methods.  Note that the actual size of this is count.
	 */
	struct objc_method        methods[];
};
