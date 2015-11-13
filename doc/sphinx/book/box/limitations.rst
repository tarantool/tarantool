-------------------------------------------------------------------------------
                            Limitations
-------------------------------------------------------------------------------

* :ref:`Number of fields in an index <lim_fields_in_index>`
* :ref:`Number of indexes in a space <lim_indexes_in_space>`
* :ref:`Number of fields in tuple <lim_fields_in_tuple>`
* :ref:`Number of spaces <lim_number_of_spaces>`
* :ref:`Number of connections <lim_number_of_connections>`
* :ref:`Space size <lim_space_size>`
* :ref:`Update operations count <lim_update_ops>`
* :ref:`Number of users and roles <lim_users_and_roles>`
* :ref:`Length of an index name or space name or user name<lim_length>`
* :ref:`Limitations which are only applicable for the sophia storage engine<lim_sophia>`

.. _lim_fields_in_index:

**Number of fields in an index**

    For BITSET indexes, the maximum is 1. For TREE or HASH indexes, the maximum
    is 255 (``box.schema.INDEX_PART_MAX``). For RTREE indexes, the
    maximum is 1 but the field is an ARRAY.

.. _lim_indexes_in_space:

**Number of indexes in a space**

    10 (``box.schema.INDEX_MAX``).

.. _lim_fields_in_tuple:

**Number of fields in a tuple**

    The theoretical maximum is 2147483647 (``box.schema.FIELD_MAX``). The
    practical maximum is whatever is specified by the space's
    :func:`space_object:field_count() <space_object.field_count>`
    member, or the maximum tuple length.

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

    32

.. _lim_length:

**Length of an index name or space name or user name**

    32 (``box.schema.NAME_MAX``).

.. _lim_sophia:

**Limitations which are only applicable for the sophia storage engine**

    The maximum number of indexes in a space is
    always 1, that is, secondary indexes are not supported. Indexes must be
    type=TREE, that is, the options type=HASH or type=RTREE or type=BITSET are
    not supported. Indexes must be unique, that is, the option unique=false
    is not supported. The ``alter()`` and ``count()`` and
    ``min()`` and ``max()`` and ``random()`` and ``auto_increment()`` functions
    are not supported. Temporary spaces are not supported.
    The maximum number of fields in an index is 8.

