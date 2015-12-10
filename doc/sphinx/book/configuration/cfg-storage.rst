    :confval:`slab_alloc_arena`, |br|
    :confval:`slab_alloc_minimal`, |br|
    :confval:`slab_alloc_maximal`, |br|
    :confval:`slab_alloc_factor`, |br|
    :confval:`sophia` |br|

.. confval:: slab_alloc_arena

    How much memory Tarantool allocates to actually store tuples, in gigabytes.
    When the limit is reached, INSERT or UPDATE requests begin failing with
    error :errcode:`ER_MEMORY_ISSUE`. While the server does not go beyond the defined
    limit to allocate tuples, there is additional memory used to store indexes
    and connection information. Depending on actual configuration and workload,
    Tarantool can consume up to 20% more than the limit set here.

    Type: float |br|
    Default: 1.0 |br|
    Dynamic: no |br|

.. confval:: slab_alloc_minimal

    Size of the smallest allocation unit. It can be tuned down if most
    of the tuples are very small

    Type: integer |br|
    Default: 64 |br|
    Dynamic: no |br|

.. confval:: slab_alloc_maximal

    Size of the largest allocation unit. It can be tuned up if it
    is necessary to store large tuples.

    Type: integer |br|
    Default: 1048576 |br|
    Dynamic: no |br|

.. confval:: slab_alloc_factor

    Use slab_alloc_factor as the multiplier for computing the sizes of memory
    chunks that tuples are stored in. A lower value may result in less wasted
    memory depending on the total amount of memory available and the
    distribution of item sizes.

    Type: float |br|
    Default: 1.1 |br|
    Dynamic: no |br|

.. confval:: sophia

    The default sophia configuration can be changed with

    .. cssclass:: highlight
    .. parsed-literal::

        sophia = {
          page_size = *number*,
          threads = *number*,
          node_size = *number*,
          memory_limit = *number*,
        }`

    This method may change in the future.

    Type: table |br|
    Default: (see above) |br|
    Dynamic: no |br|
