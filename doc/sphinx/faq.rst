:title: FAQ's
:slug: faq
:save_as: doc/faq.html
:template: documentation_rst

-------------------------------------------------------------------------------
                                   FAQ
-------------------------------------------------------------------------------
.. container:: faq

    :Q: Why Tarantool?
    :A: Tarantool is a result of a long trial and error process within Mail.Ru. It's
        the Nth generation of a family of custom in-memory data servers, developed for
        various web applications. Besides, when Tarantool development started (2008)
        there were no stable and sufficiently functional open source alternative.


    :Q: Why Lua?
    :A: Lua is a lightweight, fast, extensible multi-paradigm language. Lua also happens
        to be very easy to embed. Lua coroutines relate very closely to Tarantool fibers,
        and Lua architecture works well with Tarantool internals. Lua is the first, but
        hopefully not the last, stored program language for Tarantool.


    :Q: What's the key advantage of Tarantool?
    :A: Tarantool provides a rich database feature set (HASH, TREE, RTREE, BITSET indexes,
        secondary indexes, composite indexes, transactions, triggers, asynchronous replication)
        in a flexible environment of a Lua interpreter.
        
        These two properties make it possible to code fast, atomic and reliable in-memory
        data servers which handle non-trivial application-specific logic. The win over
        traditional SQL servers is in performance: low-overhead, lock-free architecture
        means Tarantool can serve an order of magnitude more requests per second, on
        comparable hardware. The win over NoSQL alternatives is in flexibility: Lua
        allows flexible processing of data stored in a compact, denormalized format.


    :Q: What are your development plans?
    :A: We continuously improve server performance. On the feature front, automatic
        sharding and online upgrade are the two major goals of 2015.


    :Q: Who is developing Tarantool?
    :A: There is a small engineering team employed by Mail.Ru -- check out our commit
        logs on github. The development is fully open, and most of the connectors
        authors and maintainers for different distributions are from the community.


    :Q: How serious is Mail.Ru about Tarantool?
    :A: Tarantool is an open source project, distributed under a BSD license, and as
        such does not depend on any one sponsor. However, it is currently an integral
        part of Mail.Ru backbone, so it gets a lot of support from Mail.Ru.


    :Q: What happens when Tarantool runs out of memory?
    :A: The server stops accepting updates until more memory is available. Read and
        delete requests continue to be served without difficulty.
