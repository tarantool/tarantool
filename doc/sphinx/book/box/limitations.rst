-------------------------------------------------------------------------------
                            Limitations
-------------------------------------------------------------------------------

.. _lim_fields_in_index:

**Number of parts in an index**

    For TREE or HASH indexes, the maximum
    is 255 (``box.schema.INDEX_PART_MAX``). For RTREE indexes, the
    maximum is 1 but the field is an ARRAY.
    For BITSET indexes, the maximum is 1. 

    Note re storage engine: phia allows 8 parts in an index.

.. _lim_indexes_in_space:

**Number of indexes in a space**

    128 (``box.schema.INDEX_MAX``).

    Note re storage engine: phia allows 1 index in a space.

.. _lim_fields_in_tuple:

**Number of fields in a tuple**

    The theoretical maximum is 2147483647 (``box.schema.FIELD_MAX``). The
    practical maximum is whatever is specified by the space's
    :ref:`field_count <space-object-field-count>`
    member, or the maximum tuple length.

.. _lim_bytes_in_tuple:

**Number of bytes in a tuple**

    By default the value of :confval:`slab_alloc_maximal`
    is 1048576, and the maximum tuple length is approximately one quarter of that:
    approximately 262,000 bytes. To increase it, when starting the server,
    specify a larger value. For example
    :code:`box.cfg{slab_alloc_maximal=2*1048576}`.

.. _lim_number_of_spaces:

**Number of spaces**

    The theoretical maximum is 2147483647 (``box.schema.SPACE_MAX``).

.. _lim_number_of_connections:

**Number of connections**

    The practical limit is the number of file descriptors that one can set
    with the operating system.

.. _lim_space_size:

**Space size**

    The total maximum size for all spaces is in effect set by
    :confval:`slab_alloc_arena`, which in turn
    is limited by the total available memory.

.. _lim_update_ops:

**Update operations count**

    The maximum number of operations that can be in a single update
    is 4000 (``BOX_UPDATE_OP_CNT_MAX``).

.. _lim_users_and_roles:

**Number of users and roles**

    32 (BOX_USER_MAX).

.. _lim_length:

**Length of an index name or space name or user name**

    32 (``box.schema.NAME_MAX``).

.. _lim_replicas:

**Number of replicas in a cluster**

    32 (``box.schema.REPLICA_MAX``).

.. _lim_phia:

For additional limitations which apply only to the phia
storage engine, see section
:ref:`Differences between memtx and phia <phia_diff>`.

