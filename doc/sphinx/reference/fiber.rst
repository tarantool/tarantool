-------------------------------------------------------------------------------
                            Package `fiber`
-------------------------------------------------------------------------------

The ``fiber`` package allows for creating, running and managing *fibers*.

A fiber is a set of instructions which are executed with cooperative
multitasking. Fibers managed by the fiber package are associated with
a user-supplied function called the *fiber function*.
A fiber has three possible states: **running**, **suspended** or **dead**.
When a fiber is created with ``fiber.create()``, it is running.
When a fiber yields control with ``fiber.sleep()``, it is suspended.
When a fiber ends (because the fiber function ends), it is dead.

All fibers are part of the fiber registry. This registry can be searched
(``fiber.find()``) - via fiber id (fid), which is numeric.

A runaway fiber can be stopped with ``fiber_object:cancel()``. However,
``fiber_object:cancel()`` is advisory — it works only if the runaway fiber
calls ``fiber.testcancel()`` once in a while. Most box.* functions, such
as ``box.space...delete()`` or ``box.space...update()``, do call
``fiber.testcancel()`` but ``box.space...select{}`` does not. In practice,
a runaway fiber can only become unresponsive if it does many computations
and does not check whether it's been canceled.

The other potential problem comes from fibers which never get scheduled,
because they are not subscribed to any events, or because no relevant
events occur. Such morphing fibers can be killed with ``fiber.cancel()``
at any time, since ``fiber.cancel()`` sends an asynchronous wakeup event
to the fiber, and ``fiber.testcancel()`` is checked whenever such an event occurs.

Like all Lua objects, dead fibers are garbage collected. The garbage collector
frees pool allocator memory owned by the fiber, resets all fiber data, and
returns the fiber (now called a fiber carcass) to the fiber pool. The carcass
can be reused when another fiber is created.

.. module:: fiber

.. function:: create(function [, function-arguments])

    Create and start a fiber. The fiber is created and begins to run immediately.

    :param function: the function to be associated with the fiber
    :param function-arguments: what will be passed to function
    :return: created fiber object
    :rtype: userdata

    .. code-block:: lua

        fiber_object = fiber.create(function_name)

.. function:: self()

    :return: fiber object for the currently scheduled fiber.
    :rtype: userdata

.. function:: find(id)

    :param id: scalar value to find thread by.
    :return: fiber object for the specified fiber.
    :rtype: userdata

.. function:: sleep(time)

    Yield control to the scheduler and sleep for the specified number
    of seconds. Only the current fiber can be made to sleep.

    :param time: number of seconds to sleep.

.. function:: yield()

    Yield control to the scheduler. Equivalent to ``fiber.sleep(0)``.

.. function:: status()

    Return the status of the current fiber.

    :return: the status of ``fiber``. One of:
                    “dead”, “suspended”, or “running”.
    :rtype: string

.. function:: info()

    Return information about all fibers.

    :return: the name, id, and backtrace of all fibers.
    :rtype: table

.. function:: kill(id)

    Locate a fiber by its numeric id and cancel it. In other words,
    ``fiber.kill()`` combines ``fiber.find()`` and ``fiber_object:cancel()``.

    :param id: the id of the fiber to be canceled.
    :exception: the specified fiber does not exist or cancel is not permitted.

.. function:: testcancel()

    Check if the current fiber has been canceled
    and throw an exception if this is the case.

.. class:: fiber_object

    .. method:: id()

        :param self: fiber object, for example the fiber
                     object returned by ``fiber.create``
        :return: id of the fiber.
        :rtype: number

    .. method:: name()

        :param self: fiber object, for example the fiber
                     object returned by ``fiber.create``
        :return: name of the fiber.
        :rtype: number

    .. method:: name(name)

        Change the fiber name. By default the Tarantool server's
        interactive-mode fiber is named 'interactive' and new
        fibers created due to ``fiber.create`` are named 'lua'.
        Giving fibers distinct names makes it easier to
        distinguish them when using ``fiber.info``.

        :param self: fiber object, for example the fiber
                     object returned by ``fiber.create``
        :param string name: the new name of the fiber.
        :return: nil

    .. method:: status()

        Return the status of the specified fiber.

        :param self: fiber object, for example the fiber
                     object returned by ``fiber.create``
        :return: the status of fiber. One of: “dead”,
                 “suspended”, or “running”.
        :rtype: string

    .. method:: cancel()

        Cancel a fiber. Running and suspended fibers can be canceled.
        After a fiber has been canceled, attempts to operate on it will
        cause errors, for example ``fiber_object:id()`` will cause
        "error: the fiber is dead".

        :param self: fiber object, for example the fiber
                     object returned by ``fiber.create``
        :return: nil

        :exception: cancel is not permitted for the specified fiber object.

.. function:: time()

    :return: current system time (in seconds since the epoch) as a Lua
             number. The time is taken from the event loop clock,
             which makes this call very cheap, but still useful for
             constructing artificial tuple keys.
    :rtype: num

    .. code-block:: lua

        tarantool> fiber = require('fiber')
        ---
        ...
        tarantool>  fiber.time(), fiber.time()
        ---
        - 1385758759.2591
        - 1385758759.2591
        ...

.. function:: time64()

    :return: current system time (in microseconds since the epoch)
             as a 64-bit integer. The time is taken from the event
             loop clock.
    :rtype: num

    .. code-block:: lua

        tarantool> fiber = require('fiber')
        ---
        ...
        tarantool> fiber.time(), fiber.time64()
        ---
        - 1385758828.9825
        - 1385758828982485
        ...

=================================================
                   Example
=================================================

Make the function which will be associated with the fiber. This function
contains an infinite loop ("while 0 == 0" is always true). Each iteration
of the loop adds 1 to a global variable named gvar, then goes to sleep for
2 seconds. The sleep causes an implicit ``fiber.yield()``.

.. code-block:: lua

    tarantool> fiber = require('fiber')
    tarantool> console = require('console'); console.delimiter('!')
    tarantool> function function_x()
            ->   gvar = 0
            ->   while 0 == 0 do
            ->     gvar = gvar + 1
            ->     fiber.sleep(2)
            ->     end
            ->   end!
    ---
    ...
    tarantool> console.delimiter('')!

Make a fiber, associate function_x with the fiber, and start function_x.
It will immediately "detach" so it will be running independently of the caller.

.. code-block:: lua

    tarantool> fiber_of_x = fiber.create(function_x)
    ---
    ...

Get the id of the fiber (fid), to be used in later displays.

.. code-block:: lua

    tarantool> fid = fiber_of_x:id()
    ---
    ...

Pause for a while, while the detached function runs. Then ... Display the fiber
id, the fiber status, and gvar (gvar will have gone up a bit depending how long
the pause lasted). The status is suspended because the fiber spends almost all
its time sleeping or yielding.

.. code-block:: lua

    tarantool> print('#',fid,'. ',fiber_of_x:status(),'. gvar=',gvar)
    # 102 .  suspended . gvar= 399
    ---
    ...

Pause for a while, while the detached function runs. Then ... Cancel the fiber.
Then, once again ... Display the fiber id, the fiber status, and gvar (gvar
will have gone up a bit more depending how long the pause lasted). This time
the status is dead because the cancel worked.

.. code-block:: lua

    tarantool> fiber_of_x:cancel()
    ... fiber `lua' has been cancelled
    ... fiber `lua': exiting
    ---
    ...
    tarantool> print('#',fid,'. ',fiber_of_x:status(),'. gvar=',gvar)
    # 102 .  dead . gvar= 421
    ---
    ...
