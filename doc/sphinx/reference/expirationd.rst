.. _package-expirationd: 

-------------------------------------------------------------------------------
                                   Package `expirationd`
-------------------------------------------------------------------------------

For a commercial-grade example of a Lua rock that works with Tarantool, let us
look at expirationd, which Tarantool supplies on GitHub_ with an Artistic license.
The expirationd.lua program is lengthy (about 500 lines), so here we will only
highlight the matters that will be enhanced by studying the full source later.

.. code-block:: lua

    task.worker_fiber = fiber.create(worker_loop, task)
    log.info("expiration: task %q restarted", task.name)
    ...
    fiber.sleep(expirationd.constants.check_interval)
    ...

Whenever one hears "daemon" in Tarantool, one should suspect it's being done
with :doc:`fiber`. The program is making a fiber and turning control over to it so
it runs occasionally, goes to sleep, then comes back for more.

.. code-block:: lua

    for _, tuple in scan_space.index[0]:pairs(nil, {iterator = box.index.ALL}) do
    ...
            if task.is_tuple_expired(task.args, tuple) then
            task.expired_tuples_count = task.expired_tuples_count + 1
            task.process_expired_tuple(task.space_id, task.args, tuple)
    ...

The "for" instruction can be translated as "iterate through the index of the
space that is being scanned", and within it, if the tuple is "expired" (for
example, if the tuple has a timestamp field which is less than the current time),
process the tuple as an expired tuple.

.. code-block:: lua

    -- default process_expired_tuple function
    local function default_tuple_drop(space_id, args, tuple)
        local key = fun.map(
            function(x) return tuple[x.fieldno] end,
            box.space[space_id].index[0].parts
        ):totable()
        box.space[space_id]:delete(key)
    end

Ultimately the tuple-expiry process leads to ``default_tuple_drop()``
which does a "delete" of a tuple from its original space.
First the fun :ref:`fun <package-fun>` package is used,
specifically fun.map_.
Remembering that :codenormal:`index[0]` is always the space's primary key,
and :codenormal:`index[0].parts[`:codeitalic:`N`:codenormal:`].fieldno`
is always the field number for key part :codeitalic:`N`,
fun.map() is creating a table from the primary-key values of the tuple.
The result of fun.map() is passed to :func:`space_object:delete() <space_object.delete>`.

.. code-block:: lua

    local function expirationd_run_task(name, space_id, is_tuple_expired, options)
    ...

At this point, if the above explanation is worthwhile, it's clear that
``expirationd.lua`` starts a background routine (fiber) which iterates through
all the tuples in a space, sleeps cooperatively so that other fibers can
operate at the same time, and - whenever it finds a tuple that has expired
- deletes it from this space. Now the
"``expirationd_run_task()``" function can be used
in a test which creates sample data, lets the
daemon run for a while, and prints results.

For those who like to see things run, here are the exact steps to get
expirationd through the test.

1. Get ``expirationd.lua``. There are standard ways - it is after all part
   of a `standard rock <https://luarocks.org/modules/rtsisyk/expirationd>`_  - but for this purpose just copy the contents of
   expirationd.lua_ to a default directory.
2. Start the Tarantool server as described before.
3. Execute these requests:

.. code-block:: lua

     fiber = require('fiber')
     expd = require('expirationd')
     box.cfg{}
     e = box.schema.space.create('expirationd_test')
     e:create_index('primary', {type = 'hash', parts = {1, 'NUM'}})
     e:replace{1, fiber.time() + 3}
     e:replace{2, fiber.time() + 30}
     function is_tuple_expired(args, tuple)
       if (tuple[2] < fiber.time()) then return true end
       return false
       end
     expd.run_task('expirationd_test', e.id, is_tuple_expired)
     retval = {}
     fiber.sleep(2)
     expd.task_stats()
     fiber.sleep(2)
     expd.task_stats()
     expd.kill_task('expirationd_test')
     e:drop()
     os.exit()

The database-specific requests (``cfg``,
:ref:`space.create <schema-space-create>`,
:func:`create_index <space_object.create_index>`)
should already be familiar.

The function which will be supplied to expirationd is
:codenormal:`is_tuple_expired`, which is saying
"if the second field of the tuple is less than the
:ref:`current time <fiber-time>`  , then return true, otherwise return false".

The key for getting the rock rolling is
``expd = require('expirationd')``. The "``require``" function is what reads in
the program; it will appear in many later examples in this manual, when it's
necessary to get a package that's not part of the Tarantool kernel. After the
Lua variable expd has been assigned the value of the expirationd package, it's
possible to invoke the package's ``run_task()`` function.

After :ref:`sleeping <fiber-sleep>` for two seconds, when the task has had time to do its iterations through the spaces,
``expd.task_stats()`` will print out a report showing how many tuples have expired --
"expired_count: 0".
After sleeping for two more seconds, ``expd.task_stats()`` will print out a report showing
how many tuples have expired -- 
"expired_count: 1".
This shows that the is_tuple_expired() function eventually returned "true"
for one of the tuples, because its timestamp field was more than
three seconds old.

Of course, expirationd can be customized to do different things
by passing different parameters, which will be evident after looking in more detail
at the source code.


.. _rock: http://rocks.tarantool.org/
.. _expirationd.lua: https://github.com/tarantool/expirationd/blob/master/expirationd.lua
.. _GitHub: https://github.com/tarantool/expirationd/blob/master/expirationd.lua
.. _fun.map: http://rtsisyk.github.io/luafun/transformations.html#fun.map

