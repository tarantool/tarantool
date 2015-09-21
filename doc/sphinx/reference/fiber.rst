-------------------------------------------------------------------------------
                            Package `fiber`
-------------------------------------------------------------------------------

The ``fiber`` package allows for creating, running and managing *fibers*.

A fiber is a set of instructions which are executed with cooperative
multitasking. Fibers managed by the fiber package are associated with
a user-supplied function called the *fiber function*.
A fiber has three possible states: **running**, **suspended** or **dead**.
When a fiber is created with :code:`fiber.create()`, it is running.
When a fiber yields control with :code:`fiber.sleep()`, it is suspended.
When a fiber ends (because the fiber function ends), it is dead.

All fibers are part of the fiber registry. This registry can be searched
with :code:`fiber.find()` - via fiber id (fid), which is a numeric identifier.

A runaway fiber can be stopped with :code:`fiber_object:cancel()`. However,
:code:`fiber_object:cancel()` is advisory — it works only if the runaway fiber
calls :code:`fiber.testcancel()` occasionally. Most box.* functions, such
as :code:`box.space...delete()` or :code:`box.space...update()`, do call
:code:`fiber.testcancel()` but :code:`box.space...select{}` does not. In practice,
a runaway fiber can only become unresponsive if it does many computations
and does not check whether it has been canceled.

The other potential problem comes from fibers which never get scheduled,
because they are not subscribed to any events, or because no relevant
events occur. Such morphing fibers can be killed with :code:`fiber.cancel()`
at any time, since :code:`fiber.cancel()` sends an asynchronous wakeup event
to the fiber, and :code:`fiber.testcancel()` is checked whenever such a
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

    | Return: created fiber object
    | Rtype: userdata


    | EXAMPLE
    | :codenormal:`tarantool>` :codebold:`fiber = require('fiber')`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`function function_name() fiber.sleep(1000) end`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`fiber_object = fiber.create(function_name)`
    | :codenormal:`---`
    | :codenormal:`...`


.. function:: self()

    | Return: fiber object for the currently scheduled fiber.
    | Rtype: userdata


    | EXAMPLE
    | :codenormal:`tarantool>` :codebold:`fiber.self()`
    | :codenormal:`---`
    | :codenormal:`- status: running`
    | |nbsp| |nbsp| :codenormal:`name: interactive`
    | |nbsp| |nbsp| :codenormal:`id: 101`
    | :codenormal:`...`

.. function:: find(id)

    :param id: numeric identifier of the fiber.

    | Return: fiber object for the specified fiber.
    | Rtype: userdata


    | EXAMPLE
    | :codenormal:`tarantool>` :codebold:`fiber.find(101)`
    | :codenormal:`---`
    | :codenormal:`- status: running`
    | |nbsp| |nbsp| :codenormal:`name: interactive`
    | |nbsp| |nbsp| :codenormal:`id: 101`
    | :codenormal:`...`

.. function:: sleep(time)

    Yield control to the scheduler and sleep for the specified number
    of seconds. Only the current fiber can be made to sleep.

    :param time: number of seconds to sleep.

    | Example: :codebold:`fiber.sleep(1.5)`

.. function:: yield()

    Yield control to the scheduler. Equivalent to :code:`fiber.sleep(0)`.

    | Example: :codebold:`fiber.yield()`

.. function:: status()

    Return the status of the current fiber.

    | Return: the status of :code:`fiber`. One of: “dead”, “suspended”, or “running”.
    | Rtype: string
    | Example: :codebold:`fiber.status()`

.. function:: info()

    Return information about all fibers.

    | Return: number of context switches, backtrace, id, total memory, used memory, name for each fiber.
    | Rtype: table


    | EXAMPLE
    | :codenormal:`tarantool>` :codebold:`fiber.info()`
    | :codenormal:`---`
    | :codenormal:`- 101:`
    | |nbsp| |nbsp| :codenormal:`csw: 30`
    | |nbsp| |nbsp| :codenormal:`backtrace: []`
    | |nbsp| |nbsp| :codenormal:`fid: 101`
    | |nbsp| |nbsp| :codenormal:`memory:`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`total: 65776`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`used: 0`
    | |nbsp| |nbsp| :codenormal:`name: interactive`
    | :codenormal:`...`

.. function:: kill(id)

    Locate a fiber by its numeric id and cancel it. In other words,
    :code:`fiber.kill()` combines :code:`fiber.find()` and :code:`fiber_object:cancel()`.

    :param id: the id of the fiber to be canceled.

    | Exception: the specified fiber does not exist or cancel is not permitted.
    | Example: :codebold:`fiber.kill(102)`

.. function:: testcancel()

    Check if the current fiber has been canceled
    and throw an exception if this is the case.

    | Example: :codebold:`fiber.testcancel()`


.. class:: fiber_object

    .. method:: id()

        :param self: fiber object, for example the fiber object returned by :code:`fiber.create`


        | Return: id of the fiber.
        | Rtype: number
        | Example: :codebold:`fiber_object:id()`

    .. method:: name()

        :param self: fiber object, for example the fiber object returned by :code:`fiber.create`

        | Return: name of the fiber.
        | Rtype: string
        | Example: :codebold:`fiber_object:name()`

    .. method:: name(name)

        Change the fiber name. By default the Tarantool server's
        interactive-mode fiber is named 'interactive' and new
        fibers created due to :code:`fiber.create` are named 'lua'.
        Giving fibers distinct names makes it easier to
        distinguish them when using :code:`fiber.info`.

        :param self: fiber object, for example the fiber
                     object returned by :code:`fiber.create`
        :param string name: the new name of the fiber.

        | Return: nil
        | Example: :codebold:`fiber_object:name('function_name')`

    .. method:: status()

        Return the status of the specified fiber.

        :param self: fiber object, for example the fiber
                     object returned by :code:`fiber.create`

        | Return: the status of fiber. One of: “dead”, “suspended”, or “running”.
        | Rtype: string
        | Example: :codebold:`fiber_object:status()`

    .. method:: cancel()

        Cancel a fiber. Running and suspended fibers can be canceled.
        After a fiber has been canceled, attempts to operate on it will
        cause errors, for example :code:`fiber_object:id()` will cause
        "error: the fiber is dead".

        :param self: fiber object, for example the fiber
                     object returned by :code:`fiber.create`

        | Return: nil
        | Exception: cancel is not permitted for the specified fiber object.
        | Example: :codebold:`fiber_object:cancel()`

.. function:: time()

    | Return: current system time (in seconds since the epoch) as a Lua
             number. The time is taken from the event loop clock,
             which makes this call very cheap, but still useful for
             constructing artificial tuple keys.
    | Rtype: num


    | EXAMPLE
    | :codenormal:`tarantool>` :codebold:`fiber.time(), fiber.time()`
    | :codenormal:`---`
    | :codenormal:`- 1385758759.2591`
    | :codenormal:`- 1385758759.2591`
    | :codenormal:`...`

.. function:: time64()

    | Return: current system time (in microseconds since the epoch)
             as a 64-bit integer. The time is taken from the event
             loop clock.
    | Rtype: num


    | EXAMPLE
    | :codenormal:`tarantool>` :codebold:`fiber.time(), fiber.time64()`
    | :codenormal:`---`
    | :codenormal:`- 1385758828.9825`
    | :codenormal:`- 1385758828982485`
    | :codenormal:`...`

=================================================
             Example Of Fiber Use
=================================================

Make the function which will be associated with the fiber. This function
contains an infinite loop ("while 0 == 0" is always true). Each iteration
of the loop adds 1 to a global variable named gvar, then goes to sleep for
2 seconds. The sleep causes an implicit :code:`fiber.yield()`.

   | :codenormal:`tarantool>` :codebold:`fiber = require('fiber')`
   | :codenormal:`tarantool>` :codebold:`console = require('console'); console.delimiter('!')`
   | :codenormal:`tarantool>` :codebold:`function function_x()`
   | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| :codebold:`gvar = 0`
   | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| :codebold:`while 0 == 0 do`
   | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| |nbsp| |nbsp| :codebold:`gvar = gvar + 1`
   | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| |nbsp| |nbsp| :codebold:`fiber.sleep(2)`
   | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| |nbsp| |nbsp| :codebold:`end`
   | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` |nbsp| |nbsp| :codebold:`end!`
   | :codenormal:`---`
   | :codenormal:`...`
   | :codenormal:`tarantool>` :codebold:`console.delimiter('')!`

Make a fiber, associate function_x with the fiber, and start function_x.
It will immediately "detach" so it will be running independently of the caller.

    | :codenormal:`tarantool>` :codebold:`fiber_of_x = fiber.create(function_x)`
    | :codenormal:`---`
    | :codenormal:`...`

Get the id of the fiber (fid), to be used in later displays.

    | :codenormal:`tarantool>` :codebold:`fid = fiber_of_x:id()`
    | :codenormal:`---`
    | :codenormal:`...`

Pause for a while, while the detached function runs. Then ... Display the fiber
id, the fiber status, and gvar (gvar will have gone up a bit depending how long
the pause lasted). The status is suspended because the fiber spends almost all
its time sleeping or yielding.

    | :codenormal:`tarantool>` :codebold:`print('#',fid,'. ',fiber_of_x:status(),'. gvar=',gvar)`
    | :codenormal:`# 102 .  suspended . gvar= 399`
    | :codenormal:`---`
    | :codenormal:`...`

Pause for a while, while the detached function runs. Then ... Cancel the fiber.
Then, once again ... Display the fiber id, the fiber status, and gvar (gvar
will have gone up a bit more depending how long the pause lasted). This time
the status is dead because the cancel worked.

    | :codenormal:`tarantool>` :codebold:`fiber_of_x:cancel()`
    | :codenormal:`... fiber `lua' has been cancelled`
    | :codenormal:`... fiber `lua': exiting`
    | :codenormal:`---`
    | :codenormal:`- error:`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`print('#',fid,'. ',fiber_of_x:status(),'. gvar=',gvar)`
    | :codenormal:`# 102 .  dead . gvar= 421`
    | :codenormal:`---`
    | :codenormal:`...`
