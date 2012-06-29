
/**
 * Metadata structure for an instance variable.
 *
 * Note: The modern Apple runtime apparently stores the alignment of the ivar
 * here.  We don't - we can compute it from the type, but it might be useful.
 *
 * It would also be good to add GC properties to this structure, and possibly
 * an assignment policy (e.g. assign / retain / copy).
 */
struct objc_ivar
{
	/**
	 * Name of this instance variable.
	 */
	const char *name;
	/**
	 * Type encoding for this instance variable.
	 */
	const char *type;
	/**
	 * The offset from the start of the object.  When using the non-fragile
	 * ABI, this is initialized by the compiler to the offset from the start of
	 * the ivars declared by this class.  It is then set by the runtime to the
	 * offset from the object pointer.  
	 */
	int         offset;
};

/**
 * A list of instance variables declared on this class.  Unlike the method
 * list, this is a single array and size.  Categories are not allowed to add
 * instance variables, because that would require existing objects to be
 * reallocated, which is only possible with accurate GC (i.e. not in C).
 */
struct objc_ivar_list 
{
	/**
	 * The number of instance variables in this list.
	 */
	int              count;
	/**
	 * An array of instance variable metadata structures.  Note that this array
	 * has count elements.
	 */
	struct objc_ivar ivar_list[];
};
