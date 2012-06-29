GNUstep Objective-C Runtime
===========================

The GNUstep Objective-C runtime is designed as a drop-in replacement for the
GCC runtime.  It supports both a legacy and a modern ABI, allowing code
compiled with old versions of GCC to be supported without requiring
recompilation.  The modern ABI adds the following features:

- Non-fragile instance variables.
- Protocol uniquing.
- Object planes support.
- Declared property introspection.

Both ABIs support the following feature above and beyond the GCC runtime:

- The modern Objective-C runtime APIs, introduced with OS X 10.5.
- Blocks (closures).
- Low memory profile for platforms where memory usage is more important than
  speed.
- Synthesised property accessors.
- Efficient support for @synchronized()
- Type-dependent dispatch, eliminating stack corruption from mismatched
  selectors.
- Support for the associated reference APIs introduced with Mac OS X 10.6.
- Support for the automatic reference counting APIs introduced with Mac OS X
  10.7

History
-------

Early work on the GNUstep runtime combined code from the GCC Objective-C
runtime, the Étoilé Objective-C runtime, Remy Demarest's blocks runtime for OS
X 10.5, and the Étoilé Objective-C 2 API compatibility framework.  All of these
aside from the GCC runtime were MIT licensed, although the GPL'd code present
in the GCC runtime meant that the combined work had to remain under the GPL.

Since then, all of the GCC code has been removed, leaving the remaining files
all MIT licensed, and allowing the entire work to be MIT licensed.  

The exception handling code uses a header file implementing the generic parts
of the Itanium EH ABI.  This file comes from PathScale's libcxxrt.  PathScale
kindly allowed it to be MIT licensed for inclusion here.

Non-Fragile Instance Variables
------------------------------

When a class is compiled to support non-fragile instance variables, the
instance_size field in the class is set to 0 - the size of the instance
variables declared on that class (excluding those inherited.  For example, an
NSObject subclass declaring an int ivar would have its instance_size set to 0 -
sizeof(int)).  The offsets of each instance variable in the class's ivar_list
field are then set to the offset from the start of the superclass's ivars.

When the class is loaded, the runtime library uses the size of the superclass
to calculate the correct size for this new class and the correct offsets.  Each
instance variable should have two other variables exported as global symbols.
Consider the following class:

@interface NewClass : SuperClass {
	int anIvar;
}
@end

This would have its instance_size initialized to 0-sizeof(int), and anIvar's
offset initialized to 0.  It should also export the following two symbols:

int __objc_ivar_offset_value_NewClass.anIvar;
int *__objc_ivar_offset_NewClass.anIvar;

The latter should point to the former or to the ivar_offset field in the ivar
metadata.  The former should be pointed to by the only element in the
ivar_offsets array in the class structure.  

In other compilation units referring to this ivar, the latter symbol should be
exported as a weak symbol pointing to an internal symbol containing the
compiler's guess at the ivar offset.  The ivar will then work as a fragile ivar
when NewClass is compiled with the old ABI.  If NewClass is compiled with the
new ABI, then the linker will replace the weak symbol with the version in the
class's compilation unit and references which use this offset will function
correctly.

If the compiler can guarantee that NewClass is compiled with the new ABI, for
example if it is declared in the same compilation unit, by finding the symbol
during a link-time optimization phase, or as a result of a command-line
argument, then it may use the __objc_ivar_offset_value_NewClass.anIvar symbol
as the ivar offset.  This eliminates the need for one load for every ivar
access.  

Protocols
---------

The runtime now provides a __ObjC_Protocol_Holder_Ugly_Hack class.  All
protocols that are referenced but not defined should be registered as
categories on this class.  This ensures that every protocol is registered with
the runtime.  

In the near future, the runtime will ensure that protocols can be looked up by
name at run time and that empty protocol definitions have their fields updated
to match the defined version.

Protocols have been extended to provide space for introspection on properties
and optional methods.  These fields only exist on protocols compiled with a
compiler that supports Objective-C 2.  To differentiate the two, the isa
pointer for new protocols will be set to the Protocol2 class.

Blocks
------

The GNUstep runtime provides the run time support required for Apple's blocks
(closures) extension to C.  This follows the same ABI as OS X 10.6.

Fast Proxies and Cacheable Lookups
----------------------------------

The new runtime provides two mechanisms for faster lookup.  The older
Vobjc_msg_lookup() function, which returns an IMP, is still supported, however
it is no longer recommended.  The new lookup functions is:

Slot_t objc_msg_lookup_sender(id *receiver, SEL selector, id sender)

The receiver is passed by pointer, and so may be modified during the lookup
process.  The runtime itself will never modify the receiver.  The following
hook is provided to allow fast proxy support:

id (*objc_proxy_lookup)(id receiver, SEL op);

This function takes an object and selector as arguments and returns a new
objects.  The lookup will then be re-run and the final message should be sent to
the new object.

The returned Slot_t from the new lookup function is a pointer to a structure
which contains both an IMP and a version (among other things).  The version is
incremented every time the method is overridden, allowing this to be cached by
the caller.  User code wishing to perform IMP caching may use the old mechanism
if it can guarantee that the IMP will not change between calls, or the newer
mechanism.  Note that a modern compiler should insert caching automatically,
ideally with the aid of run-time profiling results.  To support this, a new hook
has been added:

Slot_t objc_msg_forward3(id receiver, SEL op);

This is identical to objc_msg_forward2(), but returns a pointer to a slot,
instead of an IMP.  The slot should have its version set to 0, to prevent
caching.

Object Planes
-------------

Object planes provide interception points for messages between groups of
related objects.  They can be thought of as similar to processes, with mediated
inter-plane communication.  A typical use-case for an object plane is to
automatically queue messages sent to a thread, or to record every message sent
to model objects.  Planes can dramatically reduce the number of proxy objects
required for this kind of activity.

The GNUstep runtime adds a flag to class objects indicating that their
instances are present in the global plane.  All constant strings, protocols,
and classes are in the global plane, and may therefore be sent and may receive
messages bypassing the normal plane interception mechanism.  

The runtime library does not provide direct support for planes, it merely
provides the core components required to implement support for planes in
another framework.  Two objects are regarded as being in the same plane when
they words immediately before their isa pointers are the same.  In this case,
the runtime's usual dispatch mechanisms will be used.  In all other cases, the
runtime will delegate message lookup to another library via the following hook:

Slot_t (*objc_plane_lookup)(id *receiver, SEL op, id sender);

From the perspective of the runtime, the plane identifier is opaque.  In
GNUstep, it is a pointer to an NSZone structure.

Threading
---------

The threading APIs from GCC libobjc are not present in this runtime.  They were
buggy, badly supported, inadequately tested, and irrelevant now that there are
well tested thread abstraction layers, such as the POSIX thread APIs and the
C1x thread functions.  The library always runs in thread-safe mode.  The same
functions for locking the runtime mutex are still supported, but their use any
mutex not exported by the runtime library is explicitly not supported.  The
(private) lock.h header is used to abstract the details of different threading
systems sufficiently for the runtime.  This provides mechanisms for locking,
unlocking, creating, and destroying mutex objects.  

Type-Dependent Dispatch
-----------------------

Traditionally, Objective-C method lookup is done entirely on the name of the
method.  This is problematic when the sender and receiver of the method
disagree on the types of a method.  

For example, consider a trivial case where you have two methods with the same
name, one taking an integer, the other taking a floating point value.  Both
will pass their argument in a register on most platforms, but not the same
register.  If the sender thinks it is calling one, but is really calling the
other, then the receiver will look in the wrong register and use a nonsense
value.  The compiler will often not warn about this.

This is a relatively benign example, but if the mismatch is between methods
taking or returning a structure and those only using scalar arguments and
return then the call frame layout will be so different that the result will be
stack corruption, possibly leading to security holes.

If you compile the GNUstep runtime with type-dependent dispatch enabled, then
sending a message with a typed selector will only ever invoke a method with the
same types.  Sending a message with an untyped selector will invoke any method
with a matching name, although the slot returned from the lookup function will
contain the types, allowing the caller to check them and construct a valid call
frame, if required.

If a lookup with a typed selector matches a method with the wrong types, the
runtime will call a handler.  This handler, by default, prints a helpful
message and exits.  LanguageKit provides an alternative version which
dynamically generates a new method performing the required boxing and calling
the original.

Exception ABI Changes
---------------------

The non-fragile ABI makes a small change to the exception structure.  The old
GCC ABI was very poorly designed.  It attempted to do things in a clever way,
avoiding the need to call functions on entering and exiting catch blocks, but
in doing so completely broke support for throwing foreign (e.g. C++) exceptions
through Objective-C stack frames containing an @finally or @catch(id) block.
It also could not differentiate between @catch(id) (catch any object type) and
@catch(...) (catch any exception and discard it).

The new ABI makes a small number of changes.  Most importantly, @catch(id) is
now indicated by using the string "@id" as the type ID, in the same way that
"Foo" is used for @catch(Foo*).  Catchalls remain identified by a NULL pointer
in this field, as with the GCC ABI.  The runtime will still deliver the
exception object to catchalls, for interoperability with old code and with the
old ABI, but this comes with all of the same problems that the old ABI had
(i.e. code using this ABI will break in exciting and spectacular ways when
mixed with C++).

The runtime provides a hook, _objc_class_for_boxing_foreign_exception(), which
takes an exception class (64-bit integer value) as an argument, and returns a
class for boxing exceptions using this ABI.  The returned class must implement
a +exceptionWithForeignException: method, taking a pointer to the ABI-defined
generic exception structure as the argument.  It should also implement a
-rethrow method, used for rethrowing the exception.  If this is omitted, then
the boxed version of the exception, rather than the original, will propagate
out from @finally blocks.

If a catch block exists that handles this class, then it will box foreign
exceptions and allow them to propagate through any @finally directives.  Boxed
exceptions will only match explicit catch statements.  To fully understand the
semantics, we'll take a look at some examples.  These all use GNUstep's
`CXXException` class, which boxes C++ exceptions, and this simple C++ function:

	extern "C" void throwcxx()
	{
		throw 1;
	}

This exception will be caught, boxed, and then silently discarded by a catchall:

	@try
	{
		throwcxx();
	}
	@catch(...)
	{
		// This will be reached, then the exception propagation stops.
	}

If an id catch block is encountered, it will be ignored, but @finally blocks
will still be called:

	@try
	{
		throwcxx();
	}
	@catch(id anyObject)
	{
		// Code here is not reached.
	}
	@finally
	{
		// This will be reached, then the exception propagation continues.
	}

The `CXXException` class is a subclass of `NSObject`, but catch statements for
the superclass will not be hit:

	@try
	{
		throwcxx();
	}
	@catch(NSObject *anyObject)
	{
		// Code here is not reached.
	}
	@catch(CXXException *boxedForeign)
	{
		// Code here is reached.
	}

As of version 1.3, the runtime also provides a unified exception model for
Objective-C++.  This allows C++ `catch` statements and Objective-C `@catch`
statements to catch Objective-C objects thrown with `@throw` or `throw`.

This required some small changes to the ABI.  Both `@try` and `try` must use
the same personality function in Objective-C++ code, because otherwise things
like nested blocks are not possible.  The unwind library must be able to map
from any instruction pointer value to a single personality function, and
without a unified personality function, it would not be able to in code like
this:

	@try
	{
		try
		{
			// What personality function should be used when unwinding from
			// here?
			objc_exception_throw(@"foo");
		}
		catch (int i) {}
	}
	catch (id foo) {}

If there is a single personality function, there must be a single format for
language-specific data.  The current C++ format is more expressive than the
Objective-C format, so we used it directly, with one extension.  C++ landing
pads are identified by a `std::type_info` subclass in the type info tables for
exception unwinding.  We provide two subclasses of this: 

	gnustep::libobjc::__objc_id_type_info;
	gnustep::libobjc::__objc_class_type_info;

The first is used for identifying `id`-typed throws and catches.  The second is
for identifying Objective-C classes.  All `id` throws use the singleton
instance of the first class, exported as `__objc_id_type_info` (with C
linkage).  Type info for classes should generate an instance of the second
class, with the name `__objc_eh_typeinfo_Foo` where `Foo` is the name of the
class (e.g. `NSObject` should generate `__objc_eh_typeinfo_NSObject`).  The
name field should be set to the name of the class, via a global variable named
`__objc_eh_typename_Foo`.  Both should have link-once ODR linkage, so that the
linker will ensure that they are unique and pointer comparison can be used to
test for equality (required by the C++ personality function).

In Objective-C++ code, the personality function is:

	__gnustep_objcxx_personality_v0()

This is a very thin wrapper around the C++ personality function.  If it is
called with an exception coming from Objective-C code, then it wraps it in a
__cxa_exception structure (defined by the C++ ABI spec).  For any other
exception type (including C++ exceptions), it passes it directly to the C++
personality function.

The Objective-C personality function was also modified slightly so that any
incoming C++ exception that was has type info indicating that it's an
Objective-C type is treated as an Objective-C object, for the purpose of
exception delivery.

Low Memory Profile
------------------

The dispatch tables for each class, in spite of using a more space-efficient
sparse array implementation than GCC libobjc, can still use quite a lot of
memory.  The NeXT runtime avoided this problem by not providing dispatch tables
at all.  Instead, it did a linear search of the method lists, caching a few
results.  Although this is O(n), it performed reasonably well.  Most message
sends are to a small number of methods.  For example, an NSMutableDictionary is
most often sent -objectForKey: and -setObject:forKey:.  If these two methods
are in the cache, then the O(n) algorithm is almost never used.

The GNUstep runtime's low memory profile stores the slots in a sorted array.
This means that the worst case performance is O(log(n)) in terms of the number
of methods, as the uncached lookup proceeds using a binary search.

If you compile the GNUstep runtime with the low memory profile, it uses a
similar strategy.  The caches use the same slot-caching mechanism described
above and can be combined with caching at the call site.  The runtime will not
create dispatch tables, which can save a few MB of RAM in projects with a lot
of classes, but can make message sending a lot slower in the worst case.

To enable the low memory profile, add low_memory=yes to your make command line.

Objective-C 2 Features
----------------------

The runtime now provides implementations of the functions required for the
@synchronized directive, for property accessors, and for fast enumeration.  The
public runtime function interfaces now match those of OS X.

Garbage Collection
------------------

As of version 1.5, the runtime support Apple-compatible garbage collection
semantics.  The `objc/objc-auto.h` header describes the interfaces to garbage
collection.  This contains some extensions to Apple's API, required to
implement parts of Cocoa that rely on private interfaces between Cocoa, Apple
libobjc, and Autozone.

Garbage collection is implemented using the Boehm-Demers-Weiser garbage
collector.  If built with boehm_gc=no, this support will not be compiled into
the runtime.  When built with GC support, the runtime will use garbage
collection for its internal tables, irrespective of whether linked Objective-C
code is using it.

Zeroing weak references are implemented by storing the bit-flipped version of
the value (making it invisible to the collector) at the designated address.
The read barrier reads the value while holding the collector lock.  This
ensures that reads of weak variables never point to finalised objects.

The runtime uses the objc_assign_global() write barrier to add static roots.
Currently, GNUstep crashes if the collector relies on every write of a pointer
to a static location being through this write barrier, so this requirement is
relaxed.  It will be enabled at some point in the future.

Several environment variables can be used for debugging programs

- LIBOBJC_DUMP_GC_STATUS_ON_EXIT.  I this is set, then the program will dump
  information about the garbage collector when it exits.
- LIBOBJC_DUMP_GC_STATUS_ON_SIGNAL.  This should be set to a signal number.
  The program will dump GC statistics when it receives the corresponding signal
  (SIGUSR2 if this environment variable is set to something that is not a
  number).
- LIBOBJC_LOG_ALLOCATIONS.  This may be set to the name of a file.  The runtime
  will dump a stack trace on every allocation and finalisation to the named
  file.  This can be used to implement tools like Apple's malloc_history().
  Note: Enabling this causes a significant speed decrease.
- LIBOBJC_CANARIES.  If this environment variable is set, then every allocation
  of garbage-collected memory will have a canary value appended to it.  On
  finalisation, the runtime will check that this value has not been modified,
  and abort if it has.  This can help to catch heap buffer overflows.  It is
  most useful when debugging.

Automatic Reference Counting
----------------------------

As of version 1.5, the runtime provides support for automatic reference
counting (ARC).  This uses the same runtime interface as documented by the ABI
supplement here:

http://clang.llvm.org/docs/AutomaticReferenceCounting.html#runtime

The runtime implements the following optimisations:

- Classes that have ARC-compliant retain, release, and autorelease methods will
  never see them called from ARC code.  Instead, equivalent code will be run
  directly.
- If an object is autoreleased, returned, and retained, it is just stored in
  thread-local storage temporarily, not actually autoreleased.
- Moving weak references skips the retain / release step.

ARC requires the ability to interoperate perfectly with manual retain / release
code, including the ability for non-ARC code to implement custom reference
counting behaviour.  If an object implements -_ARCCompliantRetainRelease, then
it is advertising that its retain, release, and autorelease implementations are
ARC-compatible.  These methods may be called explicitly in non-ARC code, but
will not be called from ARC.

ARC moves autorelease pools into the runtime.  If NSAutoreleasePool exists and
does not implement a -_ARCCompatibleAutoreleasePool method, then it will be
used directly.  If it does not exist, ARC will implement its own autorelease
pools.  If it exists and does implement -_ARCCompatibleAutoreleasePool then it
must call objc_autoreleasePoolPush() and objc_autoreleasePoolPop() to manage
autoreleased object storage and call objc_autorelease() in its -addObject:
method.
