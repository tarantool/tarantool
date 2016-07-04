.. _vinyl_diff:

-------------------------------------------------------------------------------
        Differences between memtx and vinyl storage engines
-------------------------------------------------------------------------------

    The primary difference between memtx and vinyl is that
    memtx is an "in-memory" engine while vinyl is an "on-disk"
    engine. An in-memory storage engine is generally fastr,
    and the memtx engine is justifiably the default for Tarantool,
    but there are two situations where an on-disk engine such as
    vinyl would be preferable:

    (1) when the database is larger than the available memory and
    adding more memory is not a realistic option;
    (2) when the server frequently goes down due to errors
    or a simple desire to save power -- bringing the server
    back up and restoring a memtx database into memory takes time.

    Here are behavior differences which affect programmers.
    All of these differences have been noted elsewhere in
    sentences that begin with the words "Note re storage engine: vinyl".

    With memtx, the maximum number of indexes per space is 128. |br|
    With vinyl, the maximum is 1, that is, only primary indexes are supported.
    Since primary indexes are always unique, it follows that vinyl indexes must be unique.

    With memtx, the maximum number of (TREE) index-key parts is 255. |br|
    With vinyl, the maximum is 8.

    With memtx, the index type can be TREE or HASH or RTREE or BITSET. |br|
    With vinyl, the only index type is TREE.

    With memtx, field numbers for index parts may be in any order. |br|
    With vinyl, they must be in order, with no gaps, starting with field number 1.

    With memtx, for index searches, ``nil`` is considered to be equal to any scalar key-part. |br|
    With memtx, ``nil`` or missing parts are not allowed.

    With memtx, temporary spaces are supported. |br|
    With vinyl, they are not.

    With memtx, the :ref:`alter() <box_index-alter>` and :ref:`count() <box_index-count>`
    and :ref:`min() <box_index-min>` and :ref:`max() <box_index-max>` and
    :ref:`random() <box_index-random>` and :ref:`auto_increment() <box_space-auto_increment>`
    and :ref:`truncate() <box_space-truncate>` functions are supported. |br|
    With vinyl, they are not.

    With memtx, insert and replace and update will return a tuple, if successful. |br|
    With vinyl, insert and replace and update will return nil.

    With memtx, the REQ (reverse equality) comparison-operator is supported. |br|
    With vinyl, it is not.
    (This is a minor matter because on a unique index EQ and REQ do the same thing.)

    It was explained :ref:`earlier <index-yields_must_happen>` that memtx does not "yield" on a select request,
    it yields only on data-change requests. However, vinyl does yield on a select
    request, or on an equivalent such as get() or pairs(). This has significance
    for :ref:`cooperative multitasking <atomic-cooperative_multitasking>`.

    For more about vinyl, see Appendix E :ref:`vinyl <index-vinyl>`.

