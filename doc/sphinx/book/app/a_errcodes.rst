-------------------------------------------------------------------------------
                        Appendix A. List of error codes
-------------------------------------------------------------------------------

In the current version of the binary protocol, error message, which is normally
more descriptive than error code, is not present in server response. The actual
message may contain a file name, a detailed reason or operating system error code.
All such messages, however, are logged in the error log. Below follow only general
descriptions of some popular codes. A complete list of errors can be found in file
`errcode.h`_ in the source tree.

.. _errcode.h: https://github.com/tarantool/tarantool/blob/master/src/box/errcode.h

===========================================================
                List of error codes
===========================================================

    :errcode:`ER_NONMASTER`, |br|
    :errcode:`ER_ILLEGAL_PARAMS`, |br|
    :errcode:`ER_MEMORY_ISSUE`, |br|
    :errcode:`ER_WAL_IO`, |br|
    :errcode:`ER_KEY_PART_COUNT`, |br|
    :errcode:`ER_NO_SUCH_SPACE`, |br|
    :errcode:`ER_NO_SUCH_INDEX`, |br|
    :errcode:`ER_PROC_LUA`, |br|
    :errcode:`ER_FIBER_STACK`, |br|
    :errcode:`ER_UPDATE_FIELD`, |br|
    :errcode:`ER_TUPLE_FOUND` |br|

.. errcode:: ER_NONMASTER

    Can't modify data on a replication slave.

.. errcode:: ER_ILLEGAL_PARAMS

    Illegal parameters. Malformed protocol message.

.. errcode:: ER_MEMORY_ISSUE

    Out of memory: :confval:`slab_alloc_arena` limit is reached.

.. errcode:: ER_WAL_IO

    Failed to write to disk. May mean: failed to record a change in the
    write-ahead log. Some sort of disk error.

.. errcode:: ER_KEY_PART_COUNT

    Key part count is not the same as index part count

.. errcode:: ER_NO_SUCH_SPACE

    Attempt to access a space that does not exist.

.. errcode:: ER_NO_SUCH_INDEX

    The specified index does not exist for the specified space.

.. errcode:: ER_PROC_LUA

    An error inside a Lua procedure.

.. errcode:: ER_FIBER_STACK

    Recursion limit reached when creating a new fiber. This is usually an
    indicator of a bug in a stored procedure, recursively invoking itself
    ad infinitum.

.. errcode:: ER_UPDATE_FIELD

    An error occurred during update of a field.

.. errcode:: ER_TUPLE_FOUND

    Duplicate key exists in unique index ...
