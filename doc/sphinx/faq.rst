-------------------------------------------------------------------------------
                                   FAQ
-------------------------------------------------------------------------------

.. container:: faq

    :Q: Why Tarantool?
    :A: Tarantool is the latest generation of a family of in-memory data servers
        developed for web applications. It is the result of practical experience
        and trials within Mail.Ru since development began in 2008.

    :Q: Why Lua?
    :A: Lua is a lightweight, fast, extensible multi-paradigm language. Lua also happens
        to be very easy to embed. Lua coroutines relate very closely to Tarantool fibers,
        and Lua architecture works well with Tarantool internals. Lua acts well as a
        stored program language for Tarantool, although connecting with other languages
        is also easy.

    :Q: What's the key advantage of Tarantool?
    :A: Tarantool provides a rich database feature set (HASH, TREE, RTREE, BITSET indexes,
        secondary indexes, composite indexes, transactions, triggers, asynchronous replication)
        in a flexible environment of a Lua interpreter.

        These two properties make it possible to be a fast, atomic and reliable in-memory
        data server which handles non-trivial application-specific logic. The advantage over
        traditional SQL servers is in performance: low-overhead, lock-free architecture
        means Tarantool can serve an order of magnitude more requests per second, on
        comparable hardware. The advantage over NoSQL alternatives is in flexibility: Lua
        allows flexible processing of data stored in a compact, denormalized format.

    :Q: What are your development plans?
    :A: We continuously improve server performance. On the feature front, automatic
        sharding and bsync (bidirectional file synchronization)
        and a subset of SQL are the major goals for 2016.
        We have an open roadmap to which we encourage anyone to add feature requests.

    :Q: Who is developing Tarantool?
    :A: There is an engineering team employed by Mail.Ru -- check out our commit
        logs on github.com/tarantool. The development is fully open. Most of the
        connectors' authors, and the maintainers for different distributions,
        come from the wider community.

    :Q: How serious is Mail.Ru about Tarantool?
    :A: Tarantool is an open source project, distributed under a BSD license, so
        it does not depend on any one sponsor. However, it is an integral
        part of the Mail.Ru backbone, so it gets a lot of support from Mail.Ru.

    :Q: Are there problems associated with being an in-memory server?
    :A: The principal storage engine is designed for RAM plus persistent storage.
        It is immune to data loss because there is a write-ahead log.
        Its memory-allocation and compression techniques ensure there is no waste.
        And if Tarantool runs out of memory, then it will stop accepting updates until
        more memory is available, but will continue to handle read and delete
        requests without difficulty. However, for databases which are much
        larger than the available RAM space, Tarantool has a second storage engine
        that is only limited by the available disk space.
