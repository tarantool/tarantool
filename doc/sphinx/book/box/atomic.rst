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
most of the box calls, including the data-change requests
:func:`box.space...insert <space_object.insert>` or
:func:`box.space...update <space_object.update>` or
:func:`box.space...delete <space_object.delete>`, are yield points;
however, :func:`box.space...select <space_object.select>` is not.

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

.. function:: box.begin()

    Begin the transaction. Disable implicit yields until the transaction ends.
    Signal that writes to the write-ahead log will be deferred until the transaction ends.
    In effect the fiber which executes ``box.begin()`` is starting an "active
    multi-request transaction", blocking all other fibers.

.. function:: box.commit()

    End the transaction, and make all its data-change
    operations permanent.

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
