-------------------------------------------------------------------------------
                        Appendix A. List of error codes
-------------------------------------------------------------------------------

In the current version of the binary protocol, error message, which is normally
more descriptive than error code, is not present in server response. The actual
message may contain a file name, a detailed reason or operating system error code.
All such messages, however, are logged in the error log. Below follow only general
descriptions of some popular codes. A complete list of errors can be found in file
`errcode.h`_ in the source tree.

.. _errcode.h: https://github.com/tarantool/tarantool/blob/1.6/src/box/errcode.h

    .. container:: table

        **List of error codes**

        .. rst-class:: left-align-column-1
        .. rst-class:: left-align-column-2

        +-------------------+-------------------------------------------+
        | ER_NONMASTER      | Can't modify data on a replication slave. |
        +-------------------+-------------------------------------------+
        | ER_ILLEGAL_PARAMS | Illegal parameters. Malformed protocol    |
        |                   | message.                                  |
        +-------------------+-------------------------------------------+
        | ER_MEMORY_ISSUE   | Out of memory:                            |
        |                   | :confval:`slab_alloc_arena`               |
        |                   | limit has been reached.                   |
        +-------------------+-------------------------------------------+
        | ER_WAL_IO         | Failed to write to disk. May mean: failed |
        |                   | to record a change in the                 |
        |                   | write-ahead log. Some sort of disk error. |
        +-------------------+-------------------------------------------+
        | ER_KEY_PART_COUNT | Key part count is not the same as         |
        |                   | index part count                          |
        +-------------------+-------------------------------------------+
        | ER_NO_SUCH_SPACE  | The specified space does not exist.       |
        |                   |                                           |
        +-------------------+-------------------------------------------+
        | ER_NO_SUCH_INDEX  | The specified index in the specified      |
        |                   | space does not exist.                     |
        +-------------------+-------------------------------------------+
        | ER_PROC_LUA       | An error occurred inside a Lua procedure. |
        |                   |                                           |
        +-------------------+-------------------------------------------+
        | ER_FIBER_STACK    | The recursion limit was reached when      |
        |                   | creating a new fiber. This usually        |
        |                   | indicates that a stored procedure is      |
        |                   | recursively invoking itself too often.    |
        +-------------------+-------------------------------------------+
        | ER_UPDATE_FIELD   | An error occurred during update of a      |
        |                   | field.                                    |
        +-------------------+-------------------------------------------+
        | ER_TUPLE_FOUND    | A duplicate key exists in a unique        |
        |                   | index.                                    |
        +-------------------+-------------------------------------------+