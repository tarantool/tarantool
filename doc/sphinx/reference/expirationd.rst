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
space that is being scanned", and within it, if the tuple is "expired" (that
is, if the tuple has a timestamp field which is less than the current time),
process the tuple as an expired tuple.

.. code-block:: lua

    -- put expired tuple in archive
    local function put_tuple_to_archive(space_id, args, tuple)
        -- delete expired tuple
        box.space[space_id]:delete{tuple[1]}
        local email = get_field(tuple, 2)
        if args.archive_space_id ~= nil and email ~= nil then
            box.space[args.archive_space_id]:replace{email, os.time()}
        end
    end

Ultimately the tuple-expiry process leads to ``put_tuple_to_archive()``
which does a "delete" of a tuple from its original space, and an "insert"
of the same tuple into another space. Tarantool's "replace" function is
the same as an "insert" function without an error message if a tuple with
the same content already exists in the target space.

.. code-block:: lua

    function expirationd.do_test(space_id, archive_space_id)
    ...

At this point, if the above explanation is worthwhile, it's clear that
``expirationd.lua`` starts a background routine (fiber) which iterates through
all the tuples in a space, sleeps cooperatively so that other fibers can
operate at the same time, and - whenever it finds a tuple that has expired
- deletes it from this space and puts it in another space. Now the
"``do_test()``" function can be used to create some sample spaces, let the
daemon run for a while, and print results.

For those who like to see things run, here are the exact steps to get
expirationd through the test.

1. Get ``expirationd.lua``. There are standard ways - it is after all part
   of a standard rock - but for this purpose just copy the contents of
   expirationd.lua_ to a default directory.
2. Start the Tarantool server as described before.
3. Execute these requests:

.. code-block:: lua

     box.cfg{}
     a = box.schema.space.create('origin')
     a:create_index('first', {type = 'tree', parts = {1, 'NUM'}})
     b = box.schema.space.create('archive')
     b:create_index('first', {type = 'tree', parts = {1, 'STR'}})
     expd = require('expirationd')
     expd._debug = true
     expd.do_test('origin', 'archive')
     os.exit()

The database-specific requests (``cfg``, ``space.create``, ``create_index``)
should already be familiar. The key for getting the rock rolling is
``expd = require('expirationd')``. The "``require``" function is what reads in
the program; it will appear in many later examples in this manual, when it's
necessary to get a package that's not part of the Tarantool kernel. After the
Lua variable expd has been assigned the value of the expirationd package, it's
possible to invoke the package's ``do_test()`` function.

After a while, when the task has had time to do its iterations through the spaces,
``do_test()`` will print out a report showing the tuples that were originally in
the original space, the tuples that have now been moved to the archive space, and
some statistics. Of course, expirationd can be customized to do different things
by passing different parameters, which will be evident after looking in more detail
at the source code.

.. _rock: http://rocks.tarantool.org/
.. _expirationd.lua: https://github.com/tarantool/expirationd/blob/master/expirationd.lua
.. _GitHub: https://github.com/tarantool/expirationd/blob/master/expirationd.lua
