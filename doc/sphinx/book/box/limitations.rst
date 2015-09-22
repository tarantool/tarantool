-------------------------------------------------------------------------------
                            Limitations
-------------------------------------------------------------------------------

Number of fields in an index
    For BITSET indexes, the maximum is 1. For TREE or HASH indexes, the maximum
    is 255 (``box.schema.INDEX_PART_MAX``). For RTREE indexes, the number of
    fields must be either 2 or 4.

Number of indexes in a space
    10 (``box.schema.INDEX_MAX``).

Number of fields in a tuple
    The theoretical maximum is 2147483647 (``box.schema.FIELD_MAX``). The
    practical maximum is whatever is specified by the space's
    :ref:`field_count <space_object_field_count>`
    member, or the maximum tuple length.

Number of spaces
    The theoretical maximum is 2147483647 (``box.schema.SPACE_MAX``).

Number of connections
    The practical limit is the number of file descriptors that one can set
    with the operating system.

Space size
    The total maximum size for all spaces is in effect set by
    :confval:`slab_alloc_arena`, which in turn
    is limited by the total available memory.

Update operations count
    The maximum number of operations that can be in a single update
    is 4000 (``BOX_UPDATE_OP_CNT_MAX``).

Number of users and roles
    32.

Length of an index name or space name or user name
    32 (``box.schema.NAME_MAX``).

Limitations which are only applicable for the sophia storage engine
    The maximum number of indexes in a space is
    always 1, that is, secondary indexes are not supported. Indexes must be
    type=TREE, that is, the options type=HASH or type=RTREE or type=BITSET are
    not supported. Indexes must be unique, that is, the option unique=false
    is not supported. The ``alter()`` and ``count()`` and
    ``min()`` and ``max()`` and ``random()`` and ``auto_increment()`` functions
    are not supported. Temporary spaces are not supported.
