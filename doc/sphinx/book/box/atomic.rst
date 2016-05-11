.. _atomic_execution:

-------------------------------------------------------------------------------
                            Atomic execution
-------------------------------------------------------------------------------

In several places in this manual it's been noted that Lua processes occur in
fibers on a single thread. That is why there can be a guarantee of execution
atomicity. That requires emphasis.


.. _cooperative_multitasking:

===========================================================
            Cooperative multitasking environment
===========================================================

Tarantool uses cooperative multitasking: unless a running fiber deliberately
yields control, it is not preempted by some other fiber. But a running fiber
will deliberately yield when it encounters a "yield point": an explicit
`yield()` request, or an implicit yield due to an operating-system call. Any
system call which can block will be performed asynchronously, and any running
fiber which must wait for a system call will be preempted so that another
ready-to-run fiber takes its place and becomes the new running fiber. This model
makes all programmatic locks unnecessary: cooperative multitasking ensures that
there will be no concurrency around a resource, no race conditions, and
no memory consistency issues.

When requests are small, for example simple UPDATE or INSERT or DELETE or SELECT,
fiber scheduling is fair: it takes only a little time to process the request,
schedule a disk write, and yield to a fiber serving the next client.

However, a function might perform complex computations or might be written in
such a way that yields do not occur for a long time. This can lead to unfair
scheduling, when a single client throttles the rest of the system, or to
apparent stalls in request processing. Avoiding this situation is the
responsibility of the function's author. For the default memtx storage engine
some of the box calls, including the data-change requests
:func:`box.space...insert <space_object.insert>` or
:func:`box.space...update <space_object.update>` or
:func:`box.space...delete <space_object.delete>`, will usually cause yielding;
however, :func:`box.space...select <space_object.select>` will not.
A fuller description will appear in section :ref:`The Implicit Yield Rules <the-implicit-yield-rules>`.

Note re storage engine: phia has different rules: insert or update or delete
will very rarely cause a yield, but select can cause a yield.

In the absence of transactions, any function that contains yield points may see
changes in the database state caused by fibers that preempt. Then the only safe
atomic functions for memtx databases would be functions which contain only one
database request, or functions which contain a select request followed by a
data-change request.

At this point an objection could arise: "It's good that a single data-change
request will commit and yield, but surely there are times when multiple
data-change requests must happen without yielding." The standard example is the
money-transfer, where $1 is withdrawn from account #1 and deposited into
account #2. If something interrupted after the withdrawal, then the institution
would be out of balance. For such cases, the ``begin ... commit|rollback``
block was designed.

.. _box-begin:

.. function:: box.begin()

    Begin the transaction. Disable implicit yields until the transaction ends.
    Signal that writes to the write-ahead log will be deferred until the transaction ends.
    In effect the fiber which executes ``box.begin()`` is starting an "active
    multi-request transaction", blocking all other fibers.

.. _box-commit:

.. function:: box.commit()

    End the transaction, and make all its data-change
    operations permanent.

.. _box-rollback:

.. function:: box.rollback()

    End the transaction, but cancel all its data-change
    operations. An explicit call to functions outside ``box.space`` that always
    yield, such as ``fiber.yield`` or ``fiber.sleep``, will have the same effect.

The **requests in a transaction must be sent to the server as a single block**.
It is not enough to enclose them between ``begin`` and ``commit`` or ``rollback``.
To ensure they are sent as a single block: put them in a function, or put them all
on one line, or use a delimiter so that multi-line requests are handled together.

**All database operations in a transaction should use the same storage engine**.
It is not safe to access tuple sets that are defined with ``{engine='phia'}``
and also access tuple sets that are defined with ``{engine='memtx'}``,
in the same transaction.

===========================================================
                         Example
===========================================================

Assuming that in tuple set 'tester' there are tuples in which the third
field represents a positive dollar amount ... Start a transaction, withdraw from
tuple#1, deposit in tuple#2, and end the transaction, making its effects permanent.

.. code-block:: tarantoolsession

    tarantool> function txn_example(from, to, amount_of_money)
             >   box.begin()
             >   box.space.tester:update(from, {{'-', 3, amount_of_money}})
             >   box.space.tester:update(to,   {{'+', 3, amount_of_money}})
             >   box.commit()
             >   return "ok"
             > end
    ---
    ...
    tarantool> txn_example({999}, {1000}, 1.00)
    ---
    - "ok"
    ...

.. _the-implicit-yield-rules:

===========================================================
            The Implicit Yield Rules
===========================================================

The only explicit yield requests are :ref:`fiber.sleep() <fiber-sleep>` and :ref:`fiber.yield() <fiber-yield>`,
but many other requests "imply" yields because Tarantool is designed to avoid
blocking.

The implicit yield requests are:
:ref:`insert <space_insert>` :ref:`replace <space_replace>`
:ref:`update <space_update>` :ref:`upsert <space_update>` :ref:`delete <space_delete>` (the "data-change" requests),
and
functions in package
:ref:`fio <fio-section>`,
:ref:`net_box <package_net_box>`,
:ref:`console <package-console>`, or
:ref:`socket <package-socket>` (the "os" and "network" requests).
Note re storage engine: with sophia :ref:`select <space-select>` is
an implicit yield request, but data-change requests may not be.

The yield occurs just before a blocking syscall, such as a write to the Write-Ahead Log (WAL)
or a network message reception.

Implicit yield requests are disabled by :ref:`begin <box-begin>`,
and enabled again by :ref:`commit <box-commit>`.
Therefore the sequence` |br|
:codenormal:`begin` |br|
:codenormal:`implicit yield request #1` |br|
:codenormal:`implicit yield request #2` |br|
:codenormal:`implicit yield request #3` |br|
:codenormal:`commit` |br|
will not cause implicit yield until the commit occurs
(specifically: just before the writes to the WAL, which are delayed until commit time).
The commit request is not itself an implicit yield request,
it only enables yields caused by earlier implicit yield requests.

Despite their resemblance to implicit yield requests,
:ref:`truncate <space_truncate>` and :ref:`drop <space_drop>` do not cause implicit yield.
Despite their resemblance to functions of the fio package,
functions of the :ref:`os <package-os>` package do not cause implicit yield.
Despite its resemblance to commit, :ref:`rollback <box-rollback>` does not
enable yields.

If :ref:`wal_mode <confval-wal-mode>` = 'none', then implicit yielding is disabled,
because there are no writes to the WAL.

If a task is interactive -- sending requests to the server
and receiving responses -- then it involves network IO,
and therefore there is an implicit yield, even if the
request that is sent to the server is not itself an implicit yield request.
Therefore the sequence |br|
:codenormal:`select` |br|
:codenormal:`select` |br|
:codenormal:`select` |br|
causes blocking if it is inside a function or Lua program
being executed on the server, but causes yielding if it
is done as a series of transmissions from a client, including
a client which operates via telnet, via one of the connectors,
or via the MySQL and PostgreSQL rocks,
or via the interactive mode when :ref:`"Using tarantool as a client" <using-tarantool-as-a-client>`.

After a fiber has yielded and then has regained control,
it immediately issues :ref:`testcancel <fiber-testcancel>`.
