-------------------------------------------------------------------------------
                            Atomic execution
-------------------------------------------------------------------------------

In several places it's been noted that Lua processes occur in fibers on a
single thread. That is why there can be a guarantee of execution atomicity.
That requires emphasis.

===========================================================
            Cooperative multitasking environment
===========================================================

Tarantool core is built around a cooperative multi-tasking paradigm: unless a
running fiber deliberately yields control to some other fiber, it is not
preempted. “Yield points” are built into all calls from Tarantool core to the
operating system. Any system call which can block is performed in an
asynchronous manner and the fiber waiting on the system call is preempted with
a fiber ready to run. This model makes all programmatic locks unnecessary:
cooperative multitasking ensures that there is no concurrency around a resource,
no race conditions and no memory consistency issues.

When requests are small, e.g. simple UPDATE, INSERT, DELETE, SELECT, fiber
scheduling is fair: it takes only a little time to process the request, schedule
a disk write, and yield to a fiber serving the next client.

A function, however, can perform complex computations, or be written in such a
way that control is not given away for a long time. This can lead to unfair
scheduling, when a single client throttles the rest of the system, or to
apparent stalls in request processing. Avoiding this situation is the
responsibility of the function's author. Most of the box calls, such as
:func:`box.space...insert <space_object.insert>`,
:func:`box.space...update <space_object.update>`,
:func:`box.space...delete <space_object.delete>` are yield points;
:func:`box.space...select <space_object.select>`, however, is not.

It should also be noted that, in the absence of transactions, any yield in a
function is a potential change in the database state. Effectively, it's only
possible to have CAS (compare-and-swap) -like atomic stored procedures: i.e.
functions which select and then modify a record. Multiple data change requests
always run through a built-in yield point.

At this point an objection could arise: "It's good that a single data-change
request will commit and yield, but surely there are times when multiple
data-change requests must happen without yielding." The standard example is the
money-transfer, where $1 is withdrawn from account #1 and deposited into
account #2. If something interrupted after the withdrawal, then the institution
would be out of balance. For such cases, the ``begin ... commit|rollback``
block was designed.

.. function:: box.begin()

    From this point, implicit yields are suspended. In effect the fiber which
    executes ``box.begin()`` is starting an "active multi-request transaction",
    blocking all other fibers until the transaction ends. All operations within
    this transaction should use the same storage engine.

.. function:: box.commit()

    End the currently active transaction, and make all its data-change
    operations permanent.

.. function:: box.rollback()

    End the currently active transaction, but cancel all its data-change
    operations. An explicit call to functions outside ``box.space`` that always
    yield, such as ``fiber.yield`` or ``fiber.sleep``, will have the same effect.

The **requests in a transaction must be sent to the server as a single block**.
It is not enough to enclose them between ``begin`` and ``commit`` or ``rollback``.
To ensure they are sent as a single block: put them in a function, or put them all
on one line, or use a delimiter so that multi-line requests are handled together.

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
