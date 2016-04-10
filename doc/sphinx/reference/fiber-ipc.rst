-------------------------------------------------------------------------------
                                 Package `fiber-ipc`
-------------------------------------------------------------------------------

The ``fiber-ipc`` package allows sending and receiving messages between
different processes. The words "different processes" in this context
mean different connections, different sessions, or different fibers.

Call ``fiber.channel()`` to allocate space and get a channel object,
which will be called channel for examples in this section. Call the
other ``fiber-ipc`` routines, via channel, to send messages, receive
messages, or check ipc status. Message exchange is synchronous. The
channel is garbage collected when no one is using it, as with any
other Lua object. Use object-oriented syntax, for example
``channel:put(message)`` rather than ``fiber.channel.put(message)``.

.. module:: fiber

.. function:: channel([capacity])

    Create a new communication channel.

    :param int capacity: positive integer as great as the maximum number of
                         slots (spaces for ``get`` or ``put`` messages)
                         that might be pending at any given time.

    :return: new channel.
    :rtype:  userdata

.. class:: channel_object

    .. method:: put(message[, timeout])

        Send a message using a channel. If the channel is full,
        ``channel:put()`` blocks until there is a free slot in the channel.

        :param lua_object message:
        :param timeout:
        :return: If timeout is provided, and the channel doesn't become empty for
                the duration of the timeout, ``channel:put()`` returns false.
                Otherwise it returns true.
        :rtype:  boolean

    .. method:: close()

        Close the channel. All waiters in the channel will be woken up.
        All following ``channel:put()`` or ``channel:get()`` operations will
        return an error (``nil``).

    .. method:: get([timeout])

        Fetch a message from a channel. If the channel is empty,
        ``channel:get()`` blocks until there is a message.

        :param timeout:
        :return: the value placed on the channel by an earlier
                ``channel:put()``.
        :rtype:  lua_object

    .. method:: is_empty()

        Check whether the specified channel is empty (has no messages).

        :return: true if the specified channel is empty
        :rtype:  boolean

    .. method:: count()

        Find out how many messages are on the channel. The answer is 0 if the channel is empty.

        :return: the number of messages.
        :rtype:  number

    .. method:: is_full()

        Check whether the specified channel is full.

        :return: true if the specified channel is full (has no room for a new message).
        :rtype:  boolean

    .. method:: has_readers()

        Check whether the specified channel is empty and has readers waiting for
        a message (because they have issued ``channel:get()`` and then blocked).

        :return: true if blocked users are waiting. Otherwise false.
        :rtype:  boolean

    .. method:: has_writers()

        Check whether the specified channel is full and has writers waiting
        (because they have issued ``channel:put()`` and then blocked due to lack of room).

        :return: true if blocked users are waiting. Otherwise false.
        :rtype:  boolean

    .. method:: is_closed()

        :return: true if the specified channel is already closed. Otherwise false.
        :rtype:  boolean

=================================================
                    Example
=================================================

.. code-block:: lua

    fiber = require('fiber')
    channel = fiber.channel(10)
    function consumer_fiber()
        while true do
            local task = channel:get()
            ...
        end
    end

    function consumer2_fiber()
        while true do
            -- 10 seconds
            local task = channel:get(10)
            if task ~= nil then
                ...
            else
                -- timeout
            end
        end
    end

    function producer_fiber()
        while true do
            task = box.space...:select{...}
            ...
            if channel:is_empty() then
                -- channel is empty
            end

            if channel:is_full() then
                -- channel is full
            end

            ...
            if channel:has_readers() then
                -- there are some fibers
                -- that are waiting for data
            end
            ...

            if channel:has_writers() then
                -- there are some fibers
                -- that are waiting for readers
            end
            channel:put(task)
        end
    end

    function producer2_fiber()
        while true do
            task = box.space...select{...}
            -- 10 seconds
            if channel:put(task, 10) then
                ...
            else
                -- timeout
            end
        end
    end
