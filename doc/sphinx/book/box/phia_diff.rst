.. _phia_diff:

-------------------------------------------------------------------------------
        Differences between memtx and phia storage engines
-------------------------------------------------------------------------------

    The primary difference between memtx and phia is that
    memtx is an "in-memory" engine while phia is an "on-disk"
    engine. An in-memory storage engine is generally fastr,
    and the memtx engine is justifiably the default for Tarantool,
    but there are two situations where an on-disk engine such as
    phia would be preferable:

    (1) when the database is larger than the available memory and
    adding more memory is not a realistic option;
    (2) when the server frequently goes down due to errors
    or a simple desire to save power -- bringing the server
    back up and restoring a memtx database into memory takes time.

    Here are behavior differences which affect programmers.
    All of these differences have been noted elsewhere in
    sentences that begin with the words "Note re storage engine: phia".

    With memtx, the maximum number of indexes per space is 128. |br|
    With phia, the maximum is 1, that is, only primary indexes are supported.
    Since primary indexes are always unique, it follows that phia indexes must be unique.

    With memtx, the maximum number of (TREE) index-key parts is 255. |br|
    With phia, the maximum is 8.

    With memtx, the index type can be TREE or HASH or RTREE or BITSET. |br|
    With phia, the only index type is TREE.

    With memtx, field numbers for index parts may be in any order. |br|
    With phia, they must be in order, with no gaps, starting with field number 1.

    With memtx, for index searches, ``nil`` is considered to be equal to any scalar key-part. |br|
    With memtx, ``nil`` or missing parts are not allowed.

    With memtx, temporary spaces are supported. |br|
    With phia, they are not.

    With memtx, the :ref:`alter() <index_alter>` and :ref:`count() <index_count>`
    and :ref:`min() <index_min>` and :ref:`max() <index_max>` and
    :ref:`random() <index_random>` and :ref:`auto_increment() <space_auto_increment>`
    and :ref:`truncate() <space_truncate>` functions are supported. |br|
    With phia, they are not.

    With memtx, insert and replace and update will return a tuple, if successful. |br|
    With phia, insert and replace and update will return nil.

    With memtx, the REQ (reverse equality) comparison-operator is supported. |br|
    With phia, it is not.
    (This is a minor matter because on a unique index EQ and REQ do the same thing.)

    It was explained :ref:`earlier <yields_must_happen>` that memtx does not "yield" on a select request,
    it yields only on data-change requests. However, phia does yield on a select
    request, or on an equivalent such as get() or pairs(). This has significance
    for :ref:`cooperative multitasking <cooperative_multitasking>`.

    For more about phia, see Appendix E :ref:`phia <phia>`.

