-------------------------------------------------------------------------------
                            Package `fiber`
-------------------------------------------------------------------------------

The ``fiber`` package allows for creating, running and managing *fibers*.

A fiber is a set of instructions which are executed with cooperative
multitasking. Fibers managed by the fiber package are associated with
a user-supplied function called the *fiber function*.
A fiber has three possible states: **running**, **suspended** or **dead**.
When a fiber is created with :func:`fiber.create()`, it is running.
When a fiber yields control with :func:`fiber.sleep()`, it is suspended.
When a fiber ends (because the fiber function ends), it is dead.

All fibers are part of the fiber registry. This registry can be searched
with :func:`fiber.find()` - via fiber id (fid), which is a numeric identifier.

A runaway fiber can be stopped with :func:`fiber_object.cancel`. However,
:func:`fiber_object.cancel` is advisory — it works only if the runaway fiber
calls :func:`fiber.testcancel()` occasionally. Most ``box.*`` functions, such
as :func:`box.space...delete() <space_object.delete>` or
:func:`box.space...update() <space_object.update>`, do call
:func:`fiber.testcancel()` but :func:`box.space...select{} <space_object.select>`
does not. In practice, a runaway fiber can only become unresponsive if it does
many computations and does not check whether it has been canceled.

The other potential problem comes from fibers which never get scheduled,
because they are not subscribed to any events, or because no relevant
events occur. Such morphing fibers can be killed with :func:`fiber.kill()`
at any time, since :func:`fiber.kill()` sends an asynchronous wakeup event
to the fiber, and :func:`fiber.testcancel()` is checked whenever such a
wakeup event occurs.

Like all Lua objects, dead fibers are garbage collected. The garbage collector
frees pool allocator memory owned by the fiber, resets all fiber data, and
returns the fiber (now called a fiber carcass) to the fiber pool. The carcass
can be reused when another fiber is created.

.. module:: fiber

.. function:: create(function [, function-arguments])

    Create and start a fiber. The fiber is created and begins to run immediately.

    :param function: the function to be associated with the fiber
    :param function-arguments: what will be passed to function

    :Return: created fiber object
    :Rtype: userdata

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> fiber = require('fiber')
        ---
        ...
        tarantool> function function_name()
                 >   fiber.sleep(1000)
                 > end
        ---
        ...
        tarantool> fiber_object = fiber.create(function_name)
        ---
        ...


.. function:: self()

    :Return: fiber object for the currently scheduled fiber.
    :Rtype: userdata

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> fiber.self()
        ---
        - status: running
          name: interactive
          id: 101
        ...

.. function:: find(id)

    :param id: numeric identifier of the fiber.

    :Return: fiber object for the specified fiber.
    :Rtype: userdata

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> fiber.find(101)
        ---
        - status: running
          name: interactive
          id: 101
        ...

.. function:: sleep(time)

    Yield control to the scheduler and sleep for the specified number
    of seconds. Only the current fiber can be made to sleep.

    :param time: number of seconds to sleep.

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> fiber.sleep(1.5)
        ---
        ...

.. function:: yield()

    Yield control to the scheduler. Equivalent to :func:`fiber.sleep(0) <fiber.sleep>`.

    Example:

    .. code-block:: tarantoolsession

        tarantool> fiber.yield()
        ---
        ...

.. function:: status()

    Return the status of the current fiber.

    :Return: the status of ``fiber``. One of: “dead”, “suspended”, or “running”.
    :Rtype: string

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> fiber.status()
        ---
        - running
        ...

.. function:: info()

    Return information about all fibers.

    :Return: number of context switches, backtrace, id, total memory, used
             memory, name for each fiber.
    :Rtype: table

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> fiber.info()
        ---
        - 101:
            csw: 7
            backtrace: []
            fid: 101
            memory:
              total: 65776
              used: 0
            name: interactive
        ...

.. function:: kill(id)

    Locate a fiber by its numeric id and cancel it. In other words,
    :func:`fiber.kill()` combines :func:`fiber.find()` and
    :func:`fiber_object:cancel() <fiber_object.cancel>`.

    :param id: the id of the fiber to be canceled.
    :Exception: the specified fiber does not exist or cancel is not permitted.

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> fiber.kill(fiber.id())
        ---
        - error: fiber is cancelled
        ...

.. function:: testcancel()

    Check if the current fiber has been canceled
    and throw an exception if this is the case.

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> fiber.testcancel()
        ---
        - error: fiber is cancelled
        ...

.. class:: fiber_object

    .. method:: id()

        :param self: fiber object, for example the fiber object returned
                     by :func:`fiber.create`
        :Return: id of the fiber.
        :Rtype: number

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> fiber_object = fiber.self()
            ---
            ...
            tarantool> fiber_object:id()
            ---
            - 101
            ...

    .. method:: name()

        :param self: fiber object, for example the fiber object returned
                     by :func:`fiber.create`
        :Return: name of the fiber.
        :Rtype: string

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> fiber.self():name()
            ---
            - interactive
            ...

    .. method:: name(name)

        Change the fiber name. By default the Tarantool server's
        interactive-mode fiber is named 'interactive' and new
        fibers created due to :func:`fiber.create` are named 'lua'.
        Giving fibers distinct names makes it easier to
        distinguish them when using :func:`fiber.info`.

        :param self: fiber object, for example the fiber
                     object returned by :func:`fiber.create`
        :param string name: the new name of the fiber.

        :Return: nil

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> fiber.self():name('non-interactive')
            ---
            ...

    .. method:: status()

        Return the status of the specified fiber.

        :param self: fiber object, for example the fiber object returned by
                     :func:`fiber.create`

        :Return: the status of fiber. One of: “dead”, “suspended”, or “running”.
        :Rtype: string

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> fiber.self():status()
            ---
            - running
            ...

    .. method:: cancel()

        Cancel a fiber. Running and suspended fibers can be canceled.
        After a fiber has been canceled, attempts to operate on it will
        cause errors, for example :func:`fiber_object:id() <fiber_object.id>`
        will cause ``error: the fiber is dead``.

        :param self: fiber object, for example the fiber
                     object returned by :func:`fiber.create`

        :Return: nil

        Possible errors: cancel is not permitted for the specified fiber object.

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> fiber.self():cancel()
            ---
            - error: fiber is cancelled
            ...

    .. data:: storage

        Local storage within the fiber. The storage can contain any number of
        named values, subject to memory limitations. Naming may be done with
        :samp:`{fiber_object}.storage.{name}` or :samp:`fiber_object}.storage['{name}'].`
        or with a number :samp:`{fiber_object}.storage[{number}]`.
        Values may be either numbers or strings. The storage is garbage-collected
        when :samp:`{fiber_object}:cancel()` happens. |br|
        See also :data:`box.session.storage <box.session.storage>`.

        **Example:**
 
        .. code-block:: tarantoolsession

            tarantool> fiber = require('fiber')
            ---
            ...
            tarantool> function f () fiber.sleep(1000); end
            ---
            ...
            tarantool> fiber_function = fiber:create(f)
            ---
            - error: '[string "fiber_function = fiber:create(f)"]:1: fiber.create(function, ...):
                bad arguments'
            ...
            tarantool> fiber_function = fiber.create(f)
            ---
            ...
            tarantool> fiber_function.storage.str1 = 'string'
            ---
            ...
            tarantool> fiber_function.storage['str1']
            ---
            - string
            ...
            tarantool> fiber_function:cancel()
            ---
            ...
            tarantool> fiber_function.storage['str1']
            ---
            - error: '[string "return fiber_function.storage[''str1'']"]:1: the fiber is dead'
            ...

.. function:: time()

    :Return: current system time (in seconds since the epoch) as a Lua
             number. The time is taken from the event loop clock,
             which makes this call very cheap, but still useful for
             constructing artificial tuple keys.
    :Rtype: num

    **Example:**

        .. code-block:: tarantoolsession

            tarantool> fiber.time(), fiber.time()
            ---
            - 1448466279.2415
            - 1448466279.2415
            ...

.. function:: time64()

    :Return: current system time (in microseconds since the epoch)
             as a 64-bit integer. The time is taken from the event
             loop clock.
    :Rtype: num

    **Example:**

    .. code-block:: tarantoolsession

            tarantool> fiber.time(), fiber.time64()
            ---
            - 1448466351.2708
            - 1448466351270762
            ...

.. function:: info()

    Show all running fibers, with their stack. Mainly useful for debugging.

=================================================
             Example Of Fiber Use
=================================================

Make the function which will be associated with the fiber. This function
contains an infinite loop (``while 0 == 0`` is always true). Each iteration
of the loop adds 1 to a global variable named gvar, then goes to sleep for
2 seconds. The sleep causes an implicit :func:`fiber.yield()`.

.. code-block:: tarantoolsession

    tarantool> fiber = require('fiber')
    tarantool> function function_x()
             >   gvar = 0
             >   while true do
             >     gvar = gvar + 1
             >     fiber.sleep(2)
             >   end
             > end
    ---
    ...

Make a fiber, associate function_x with the fiber, and start function_x.
It will immediately "detach" so it will be running independently of the caller.

.. code-block:: tarantoolsession

    tarantool> fiber_of_x = fiber.create(function_x)
    ---
    ...

Get the id of the fiber (fid), to be used in later displays.

.. code-block:: tarantoolsession

    tarantool> fid = fiber_of_x:id()
    ---
    ...

Pause for a while, while the detached function runs. Then ... Display the fiber
id, the fiber status, and gvar (gvar will have gone up a bit depending how long
the pause lasted). The status is suspended because the fiber spends almost all
its time sleeping or yielding.

.. code-block:: tarantoolsession

    tarantool> printf('#', fid, '. ', fiber_of_x:status(), '. gvar=', gvar)
    # 102 .  suspended . gvar= 399
    ---
    ...

Pause for a while, while the detached function runs. Then ... Cancel the fiber.
Then, once again ... Display the fiber id, the fiber status, and gvar (gvar
will have gone up a bit more depending how long the pause lasted). This time
the status is dead because the cancel worked.

.. code-block:: tarantoolsession

    tarantool> fiber_of_x:cancel()
    ... fiber `lua` has been cancelled
    ... fiber `lua` exiting
    ---
    - error:
    ...
    tarantool> printf('#', fid, '. ', fiber_of_x:status(), '. gvar=', gvar)
    # 102 .  dead . gvar= 421
    ---
    ...

