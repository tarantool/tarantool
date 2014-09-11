:title: FAQ's
:slug: faq
:save_as: faq.html

-------------------------------------------------------------------------------
                           Frequently Asked Questions
-------------------------------------------------------------------------------

**Why Tarantool?**
::

    Tarantool is a result of a long trial and error process within Mail.Ru. It's
    the Nth generation of a family of custom in-memory data servers, developed for
    various web applications. Besides, when Tarantool development started (2008)
    there were no stable and sufficiently functional open source alternative.

**Why Lua?**
::

    Lua is a ligthweight, fast, extensible multi-paradigm language. Lua also happens
    to be very easy to embed. Lua coroutines relate very closely to Tarantool fibers,
    and Lua architecture works well with Tarantool internals. Lua is the first, but,
    hopefully, not the last stored program language for Tarantool.

**What's the key advantage of Tarantool?**
::

    Tarantool provides a rich database feature set (HASH, TREE BITSET indexes,
    secondary indexes, composite indexes, asynchronous replication, hot standby,
    data durability) in a flexible environment of a Lua interpreter.
    
    These two properties make it possible to code fast, atomic and reliable in-memory
    data servers which handle non-trivial application-specific logic. The win over
    traditional SQL servers is in performance: low-overhead, lock-free architecture
    means Tarantool can serve an order of magnitude more requests per second, on
    comparable hardware. The win over NoSQL alternatives is in flexibility: Lua
    allows flexible processing of data stored in a compact, denormalized format.

**What are your development plans?**
::

    We continuously improve server performance. On the feature front, automatic
    sharding and online upgrade are the two major goals of 2014.

**Who is developing Tarantool?**
::

    There is a small engineering team employed by Mail.ru -- check out our commit
    logs on github. The development is fully open, and most of the connectors
    authors and maintainers in different distributions are from the community.

**How serious is Mail.Ru about Tarantool?**
::

    Tarantool is an open source project, distributed under a BSD license, and as
    such does not depend on any one sponsor. However, it is currently and integral
    part of Mail.Ru backbone, so it gets a lot of support from Mail.ru.

**Why is Tarantool primary port number 3303?**
::

    It's a prime number which is easy to remember, because 3313, 3301, 313, 13 and
    3 are also prime numbers.

**My arena_used/items_used in SHOW SLAB output is >> 1. What does it mean and what should I do?**
::

    If the ratio of arena_used to items_used >> 1, that indicates that there is
    fragmentation accumulated by the slab allocator. Imagine there are a lot of
    small tuples stored in the system initially, and later on each tuple becomes
    bigger and doesn't fit into its old slab size. The old slabs are never
    relinquished by the allocator. Currently this can be solved only by a server restart.

**What happens when Tarantool runs out of memory?**
::

    The server stops accepting updates until more memory is available. Read and
    delete requests are served just fine.
