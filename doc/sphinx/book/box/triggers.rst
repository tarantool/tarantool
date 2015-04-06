.. _box-triggers:

-------------------------------------------------------------------------------
                            Triggers
-------------------------------------------------------------------------------

Triggers, also known as callbacks, are functions which the server executes when
certain events happen. Currently the two types of triggers are `connection triggers`_,
which are executed when a session begins or ends, and `replace triggers`_ which are
for database events,

All triggers have the following characteristics.

* They associate a function with an `event`. The request to "define a trigger"
  consists of passing the name of the trigger's function to one of the
  "on_event-name..." functions: ``on_connect()``, ``on_disconnect()``,
  or ``on_replace()``.
* They are `defined by any user`. There are no privilege requirements for defining
  triggers.
* They are called `after` the event. They are not called if the event ends
  prematurely due to an error.
* They are in `server memory`. They are not stored in the database. Triggers
  disappear when the server is shut down. If there is a requirement to make
  them permanent, then the function definitions and trigger settings should
  be part of an initialization script.
* They have `low overhead`. If a trigger is not defined, then the overhead is
  minimal: merely a pointer dereference and check. If a trigger is defined,
  then its overhead is equivalent to the overhead of calling a stored procedure.
* They can be `multiple` for one event. Triggers are executed in the reverse
  order that they were defined in.
* They must work `within the event context`. If the function contains requests
  which normally could not occur immediately after the event but before the
  return from the event, effects are undefined. For example, defining a trigger
  function as ``os.exit()`` or ``box.rollback()`` would be bringing in requests
  outside the event context.
* They are `replaceable`. The request to "redefine a trigger" consists of passing
  the names of a new trigger function and an old trigger function to one of the
  "on_event-name..." functions.

===========================================================
                    Connection triggers
===========================================================

.. function:: box.session.on_connect(trigger-function [, old-trigger-function-name])

    Define a trigger for execution when a new session is created due to an event
    such as :func:`console.connect`. The trigger function will be the first thing
    executed after a new session is created. If the trigger fails by raising an
    error, the error is sent to the client and the connection is closed.

    :param function trigger-function: function which will become the trigger function
    :param function old-trigger-function: existing trigger function which will be replaced by trigger-function
    :return: nil

    If the parameters are (nil, old-trigger-function), then the old trigger is deleted.

    .. code-block:: lua

        function f ()
            x = x + 1
        end
        box.session.on_connect(f)

    .. WARNING::

        If a trigger always results in an error, it may become impossible to
        connect to the server to reset it.

.. function:: box.session.on_disconnect(trigger-function [, old-trigger-function])

    Define a trigger for execution after a client has disconnected. If the trigger
    function causes an error, the error is logged but otherwise is ignored. The
    trigger is invoked while the session associated with the client still exists
    and can access session properties, such as box.session.id.

    :param function trigger-function: function which will become the trigger function
    :param function old-trigger-function: existing trigger function which will be replaced by trigger-function
    :return: nil

    .. code-block:: lua

        function f ()
            x = x + 1
        end
        box.session.on_disconnect(f)

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            Example
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    After the following series of requests, the server will write a message
    using the :mod:`log` package whenever any user connects or disconnects.

.. code-block:: lua

    function log_connect ()
      local log = require('log')
      local m = 'Connection. user=' .. box.session.user() .. ' id=' .. box.session.id()
      log.info(m)
    end
    function log_disconnect ()
      local log = require('log')
      local m = 'Disconnection. user=' .. box.session.user() .. ' id=' .. box.session.id()
      log.info(m)
    end
    box.session.on_connect(log_connect)
    box.session.on_disconnect(log_disconnect)

Here is what might appear in the log file in a typical installation:

.. code-block:: lua

    2014-12-15 13:21:34.444 [11360] main/103/iproto I>
        Connection. user=guest id=3
    2014-12-15 13:22:19.289 [11360] main/103/iproto I>
        Disconnection. user=guest id=3


===========================================================
                    Replace triggers
===========================================================

.. function:: box.space.<space-name>:on_replace(trigger-function [, old-trigger-function])

    Create a "``replace trigger``". The ``function-name`` will be executed whenever
    a ``replace()`` or ``insert()`` or ``update()`` or ``delete()`` happens to a
    tuple in ``<space-name>``.

    :param function trigger-function: function which will become the trigger function
    :param function old-trigger-function: existing trigger function which will be replaced by trigger-function
    :return: nil

    .. code-block:: lua

        function f ()
            x = x + 1
        end
        box.space.X:on_replace(f)

.. function:: box.space.<space-name>:run_triggers(true|false)

    At the time that a trigger is defined, it is automatically enabled - that
    is, it will be executed. Replace triggers can be disabled with
    ``box.space.space-name:run_triggers(false)`` and re-enabled with
    ``box.space.space-name:run_triggers(true)``.

    :return: nil

    .. code-block:: lua

        box.space.X:run_triggers(false)

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            Example
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following series of requests will create a space, create an index, create
a function which increments a counter, create a trigger, do two inserts, drop
the space, and display the counter value - which is 2, because the function
is executed once after each insert.

.. code-block:: lua

    s = box.schema.space.create('space53')
    s:create_index('primary', {parts = {1, 'NUM'}})
    function replace_trigger() replace_counter = replace_counter + 1 end
    s:on_replace(replace_trigger)
    replace_counter = 0
    t = s:insert{1, 'First replace'}
    t = s:insert{2, 'Second replace'}
    s:drop()
    replace_counter

The following series of requests will associate an existing function named F
with an existing space named T, associate the function a second time with the
same space (so it will be called twice), disable all triggers of T, and destroy
each trigger by replacing with ``nil``.

.. code-block:: lua

    box.space.T:on_replace(F)
    box.space.T:on_replace(F)
    box.space.T:run_triggers(false)
    box.space.T:on_replace(nil, F)
    box.space.T:on_replace(nil, F)
