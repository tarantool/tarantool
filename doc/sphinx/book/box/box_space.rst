-------------------------------------------------------------------------------
                             Package `box.space`
-------------------------------------------------------------------------------

The ``box.space`` package has the data-manipulation functions ``select``,
``insert``, ``replace``, ``update``, ``upsert``, ``delete``, ``get``, ``put``.
It also has members, such as id, and whether or not a space is enabled. Package
source code is available in file
`src/box/lua/schema.lua <https://github.com/tarantool/tarantool/blob/1.6/src/box/lua/schema.lua>`_.

A list of all ``box.space`` functions follows, then comes a list of all
``box.space`` members.

    .. container:: table

        **The functions and members of box.space**

        .. rst-class:: left-align-column-1
        .. rst-class:: left-align-column-2

        +--------------------------------------+---------------------------------+
        | Name                                 | Use                             |
        +======================================+=================================+
        | :func:`space_object:create_index()   | Create an index                 |
        | <space_object.create_index>`         |                                 | 
        +--------------------------------------+---------------------------------+
        | :func:`space_object:insert()         | Insert a tuple                  |
        | <space_object.insert>`               |                                 |
        +--------------------------------------+---------------------------------+
        | :func:`space_object:select()         | Select one or more tuples       |
        | <space_object.select>`               |                                 |
        +--------------------------------------+---------------------------------+
        | :func:`space_object:get()            | Select a tuple                  |
        | <space_object.get>`                  |                                 |
        +--------------------------------------+---------------------------------+
        | :func:`space_object:drop()           | Destroy a space                 |
        | <space_object.drop>`                 |                                 |
        +--------------------------------------+---------------------------------+
        | :func:`space_object:rename()         | Rename a space                  |
        | <space_object.rename>`               |                                 |
        +--------------------------------------+---------------------------------+
        | :func:`space_object:replace()        | Insert or replace a tuple       |
        | <space_object.replace>`              |                                 |
        +--------------------------------------+---------------------------------+
        | :func:`space_object:put()            | Insert or replace a tuple       |
        | <space_object.replace>`              |                                 |
        +--------------------------------------+---------------------------------+
        | :func:`space_object:update()         | Update a tuple                  |
        | <space_object.update>`               |                                 |
        +--------------------------------------+---------------------------------+
        | :func:`space_object:upsert()         | Update a tuple                  |
        | <space_object.upsert>`               |                                 |
        +--------------------------------------+---------------------------------+
        | :func:`space_object:delete()         | Delete a tuple                  |
        | <space_object.delete>`               |                                 |
        +--------------------------------------+---------------------------------+
        | :func:`space_object:count()          | Get count of tuples             |
        | <space_object.count>`                |                                 |
        +--------------------------------------+---------------------------------+
        | :func:`space_object:len()            | Get count of tuples             |
        | <space_object.len>`                  |                                 |
        +--------------------------------------+---------------------------------+
        | :func:`space_object:truncate()       | Delete all tuples               |
        | <space_object.truncate>`             |                                 |
        +--------------------------------------+---------------------------------+
        | :func:`space_object:inc()            | Increment a tuple's counter     |
        | <space_object.inc>`                  |                                 |
        +--------------------------------------+---------------------------------+
        | :func:`space_object:dec()            | Decrement a tuple's counter     |
        | <space_object.dec>`                  |                                 |
        +--------------------------------------+---------------------------------+
        | :func:`space_object:auto_increment() | Generate key + Insert a tuple   |
        | <space_object.auto_increment>`       |                                 |
        +--------------------------------------+---------------------------------+
        | :func:`space_object:pairs()          | Prepare for iterating           |
        | <space_object.pairs>`                |                                 |
        +--------------------------------------+---------------------------------+
        | :func:`space_object.id               | .Numeric identifier of space    |
        | <space_object.id>`                   |                                 |
        +--------------------------------------+---------------------------------+
        | :func:`space_object.enabled          | .Flag, true if space is enabled |
        | <space_object.enabled>`              |                                 |
        +--------------------------------------+---------------------------------+
        | :func:`space_object.field_count      | .Required number of fields      |
        | <space_object.field_count>`          |                                 |
        +--------------------------------------+---------------------------------+
        | :func:`space_object.index            | .Container of space's indexes   |
        | <space_object.field_count>`          |                                 |
        +--------------------------------------+---------------------------------+
        | :class:`box.space._schema`           | .(Metadata) List of schemas     |
        +--------------------------------------+---------------------------------+
        | :class:`box.space._space`            | .(Metadata) List of spaces      |
        +--------------------------------------+---------------------------------+
        | :class:`box.space._index`            | .(Metadata) List of indexes     |
        +--------------------------------------+---------------------------------+
        | :class:`box.space._user`             | .(Metadata) List of users       |
        +--------------------------------------+---------------------------------+
        | :class:`box.space._priv`             | .(Metadata) List of privileges  |
        +--------------------------------------+---------------------------------+
        | :class:`box.space._cluster`          | .(Metadata) List of clusters    |
        +--------------------------------------+---------------------------------+


.. module:: box.space

.. class:: space_object

    .. method:: create_index(index-name [, {options} ])

        Create an index. It is mandatory to create an index for a tuple set
        before trying to insert tuples into it, or select tuples from it. The
        first created index, which will be used as the primary-key index, must be
        unique.

        Parameters: :samp:`{space_object}` = an :ref:`object reference <object-reference>`;
        :codeitalic:`index_name` (type = string) = name of index, which should not be a number
        and should not contain special characters;
        :codeitalic:`options`.

        :return: index object
        :rtype:  index_object

        .. container:: table

            Options for ``space_object:create_index``:

            .. rst-class:: left-align-column-1
            .. rst-class:: left-align-column-2
            .. rst-class:: left-align-column-3
            .. rst-class:: left-align-column-4

            +---------------+--------------------+-----------------------------+---------------------+
            | Name          | Effect             | Type                        | Default             |
            +===============+====================+=============================+=====================+
            | type          | type of index      | string                      | 'TREE'              |
            |               |                    | ('HASH',     'TREE',        |                     |
            |               |                    | 'BITSET',   'RTREE')        |                     |
            |               |                    |                             |                     |
            |               |                    |                             |                     |
            |               |                    |                             |                     |
            +---------------+--------------------+-----------------------------+---------------------+
            | id            | unique identifier  | number                      | last index's id, +1 |
            +---------------+--------------------+-----------------------------+---------------------+
            | unique        | index is unique    | boolean                     | true                |
            +---------------+--------------------+-----------------------------+---------------------+
            | if_not_exists | no error if        | boolean                     | false               |
            |               | duplicate name     |                             |                     |
            +---------------+--------------------+-----------------------------+---------------------+
            | parts         | field-numbers  +   | ``{field_no, 'NUM'|'STR'}`` | ``{1, 'NUM'}``      |
            |               | types              |                             |                     |
            +---------------+--------------------+-----------------------------+---------------------+

        Possible errors: too many parts. Index '...' already exists. Primary key must be unique.

        Note re storage engine: phia supports only the TREE index type,
        and supports only one index per space,
        and supports only the unique = true option,
        and requires that field numbers be in order starting with 1.

        .. code-block:: tarantoolsession

            tarantool> s = box.space.space55
            ---
            ...
            tarantool> s:create_index('primary', {unique = true, parts = {1, 'NUM', 2, 'STR'}})
            ---
            ...

    .. _space_insert:

    .. method:: insert(tuple)

        Insert a tuple into a space.

        Parameters: :samp:`{space_object}` = an :ref:`object reference <object-reference>`;
        :codeitalic:`tuple` (type = Lua table or tuple) = tuple to be inserted.

        :return: the inserted tuple
        :rtype:  tuple

        Possible errors: If a tuple with the same unique-key value already exists,
        returns :errcode:`ER_TUPLE_FOUND`.

        Note re storage engine: phia will return nil, rather than the inserted tuple.

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> box.space.tester:insert{5000,'tuple number five thousand'}
            ---
            - [5000, 'tuple number five thousand']
            ...

    .. _space-select:

    .. method:: select(key)

        Search for a tuple or a set of tuples in the given space.

        Parameters: :samp:`{space_object}` = an :ref:`object reference <object-reference>`;
        :codeitalic:`key` (type = Lua table or scalar) = key to be matched against the index key,
        which may be multi-part.

        :return: the tuples whose primary-key fields are equal to the passed
                 field-values. If the number of passed field-values is less
                 than the number of fields in the primary key, then only the
                 passed field-values are compared, so ``select{1,2}`` will match
                 a tuple whose primary key is ``{1,2,3}``.
        :rtype:  tuple

        Possible errors: No such space; wrong type.

        **Complexity Factors:** Index size, Index type.

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> s = box.schema.space.create('tmp', {temporary=true})
            ---
            ...
            tarantool> s:create_index('primary',{parts = {1,'NUM', 2, 'STR'}})
            ---
            ...
            tarantool> s:insert{1,'A'}
            ---
            - [1, 'A']
            ...
            tarantool> s:insert{1,'B'}
            ---
            - [1, 'B']
            ...
            tarantool> s:insert{1,'C'}
            ---
            - [1, 'C']
            ...
            tarantool> s:insert{2,'D'}
            ---
            - [2, 'D']
            ...
            tarantool> -- must equal both primary-key fields
            tarantool> s:select{1,'B'}
            ---
            - - [1, 'B']
            ...
            tarantool> -- must equal only one primary-key field
            tarantool> s:select{1}
            ---
            - - [1, 'A']
              - [1, 'B']
              - [1, 'C']
            ...
            tarantool> -- must equal 0 fields, so returns all tuples
            tarantool> s:select{}
            ---
            - - [1, 'A']
              - [1, 'B']
              - [1, 'C']
              - [2, 'D']
            ...

        For examples of complex ``select`` requests, where one can specify which
        index to search and what condition to use (for example "greater than"
        instead of "equal to") and how many tuples to return, see the later section
        :ref:`index_object:select <index_object_select>`.

    .. method:: get(key)

        Search for a tuple in the given space.

        Parameters: :samp:`{space_object}` = an :ref:`object reference <object-reference>`;
        :codeitalic:`key` (type = Lua table or scalar) = key to be matched against the index
        key, which may be multi-part.

        :return: the tuple whose index key matches :codeitalic:`key`, or null.
        :rtype:  tuple

        Possible errors: If space_object does not exist.

        **Complexity Factors:** Index size, Index type,
        Number of indexes accessed, WAL settings.

        The ``box.space...select`` function returns a set
        of tuples as a Lua table; the ``box.space...get``
        function returns at most a single tuple. And it is possible to get
        the first tuple in a tuple set by appending ``[1]``.
        Therefore ``box.space.tester:get{1}`` has the same
        effect as ``box.space.tester:select{1}[1]``,
        if exactly one tuple is found.

        **Example:**

        .. code-block:: lua

            box.space.tester:get{1}

    .. _space_drop:

    .. method:: drop()

        Drop a space.

        Parameters: :samp:`{space_object}` = an :ref:`object reference <object-reference>`.

        :return: nil

        Possible errors: If space_object does not exist.

        **Complexity Factors:** Index size, Index type,
        Number of indexes accessed, WAL settings.

        **Example:**

        .. code-block:: lua

            box.space.space_that_does_not_exist:drop()

    .. method:: rename(space-name)

        Rename a space.

        Parameters::samp:`{space_object}` = an :ref:`object reference <object-reference>`;
        :codeitalic:`space-name` (type = string) = new name for space.

        :return: nil

        Possible errors: space_object does not exist.

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> box.space.space55:rename('space56')
            ---
            ...
            tarantool> box.space.space56:rename('space55')
            ---
            ...

    .. _space_replace:

    .. method:: replace(tuple)
                  put(tuple)

        Insert a tuple into a space. If a tuple with the same primary key already
        exists, ``box.space...:replace()`` replaces the existing tuple with a new
        one. The syntax variants ``box.space...:replace()`` and
        ``box.space...:put()`` have the same effect; the latter is sometimes used
        to show that the effect is the converse of ``box.space...:get()``.

        Parameters: :samp:`{space_object}` = an :ref:`object reference <object-reference>`;
        :codeitalic:`tuple` (type = Lua table or tuple) = tuple to be inserted.

        :return: the inserted tuple.
        :rtype:  tuple

        Possible errors: If a different tuple with the same unique-key
        value already exists, returns :errcode:`ER_TUPLE_FOUND`. (This
        will only happen if there is a secondary index. By default
        secondary indexes are unique.)

        **Complexity Factors:** Index size, Index type,
        Number of indexes accessed, WAL settings.

        Note re storage engine: phia will return nil, rather than the inserted tuple.

        **Example:**

        .. code-block:: lua

            box.space.tester:replace{5000, 'tuple number five thousand'}

    .. _space_update:

    .. method:: update(key, {{operator, field_no, value}, ...})

        Update a tuple.

        The ``update`` function supports operations on fields — assignment,
        arithmetic (if the field is unsigned numeric), cutting and pasting
        fragments of a field, deleting or inserting a field. Multiple
        operations can be combined in a single update request, and in this
        case they are performed atomically and sequentially. Each operation
        requires specification of a field number. When multiple operations
        are present, the field number for each operation is assumed to be
        relative to the most recent state of the tuple, that is, as if all
        previous operations in a multi-operation update have already been
        applied. In other words, it is always safe to merge multiple ``update``
        invocations into a single invocation, with no change in semantics.

        Possible operators are:

            * ``+`` for addition (values must be numeric)
            * ``-`` for subtraction (values must be numeric)
            * ``&`` for bitwise AND (values must be unsigned numeric)
            * ``|`` for bitwise OR (values must be unsigned numeric)
            * ``^`` for bitwise :abbr:`XOR(exclusive OR)` (values must be unsigned numeric)
            * ``:`` for string splice
            * ``!`` for insertion
            * ``#`` for deletion
            * ``=`` for assignment

        For ``!`` and ``=`` operations the field number can be ``-1``, meaning the last field in the tuple.


        Parameters: :samp:`{space_object}` = an :ref:`object reference <object-reference>`;
        :codeitalic:`key` (type = Lua table or scalar) = primary-key field values, must be passed as a Lua
        table if key is multi-part;
        :codeitalic:`{operator, field_no, value}` (type = table): a group of arguments for each
        operation, indicating what the operation is, what field the
        operation will apply to, and what value will be applied. The
        field number can be negative, meaning the position from the end of
        tuple (#tuple + negative field number + 1).

        :return: the updated tuple.
        :rtype:  tuple

        Possible errors: it is illegal to modify a primary-key field.

        **Complexity Factors:** Index size, Index type, number of indexes accessed, WAL
        settings.

        Note re storage engine: phia will return nil, rather than the updated tuple.

        Thus, in the instruction:

        .. code-block:: lua

            s:update(44, {{'+', 1, 55 }, {'=', 3, 'x'}})

        the primary-key value is ``44``, the operators are ``'+'`` and ``'='`` meaning
        *add a value to a field and then assign a value to a field*, the first
        affected field is field ``1`` and the value which will be added to it is
        ``55``, the second affected field is field ``3`` and the value which will be
        assigned to it is ``'x'``.

        **Example:**

        Assume that the initial state of the database is ``tester`` that has
        one tuple set and one primary key whose type is ``NUM``.
        There is one tuple, with ``field[1]`` = ``999`` and ``field[2]`` = ``'A'``.

        In the update:

        .. code-block:: lua

            box.space.tester:update(999, {{'=', 2, 'B'}})

        The first argument is ``tester``, that is, the affected space is ``tester``.
        The second argument is ``999``, that is, the affected tuple is identified by
        primary key value = 999.
        The third argument is ``=``, that is, there is one operation —
        *assignment to a field*.
        The fourth argument is ``2``, that is, the affected field is ``field[2]``.
        The fifth argument is ``'B'``, that is, ``field[2]`` contents change to ``'B'``.
        Therefore, after this update, ``field[1]`` = ``999`` and ``field[2]`` = ``'B'``.

        In the update:

        .. code-block:: lua

            box.space.tester:update({999}, {{'=', 2, 'B'}})

        the arguments are the same, except that the key is passed as a Lua table
        (inside braces). This is unnecessary when the primary key has only one
        field, but would be necessary if the primary key had more than one field.
        Therefore, after this update, ``field[1]`` = ``999`` and ``field[2]`` = ``'B'`` (no change).

        In the update:

        .. code-block:: lua

            box.space.tester:update({999}, {{'=', 3, 1}})

        the arguments are the same, except that the fourth argument is ``3``,
        that is, the affected field is ``field[3]``. It is okay that, until now,
        ``field[3]`` has not existed. It gets added. Therefore, after this update,
        ``field[1]`` = ``999``, ``field[2]`` = ``'B'``, ``field[3]`` = ``1``.

        In the update:

        .. code-block:: lua

            box.space.tester:update({999}, {{'+', 3, 1}})

        the arguments are the same, except that the third argument is ``'+'``,
        that is, the operation is addition rather than assignment. Since
        ``field[3]`` previously contained ``1``, this means we're adding ``1``
        to ``1``. Therefore, after this update, ``field[1]`` = ``999``,
        ``field[2]`` = ``'B'``, ``field[3]`` = ``2``.

        In the update:

        .. code-block:: lua

            box.space.tester:update({999}, {{'|', 3, 1}, {'=', 2, 'C'}})

        the idea is to modify two fields at once. The formats are ``'|'`` and
        ``=``, that is, there are two operations, OR and assignment. The fourth
        and fifth arguments mean that ``field[3]`` gets OR'ed with ``1``. The
        seventh and eighth arguments mean that ``field[2]`` gets assigned ``'C'``.
        Therefore, after this update, ``field[1]`` = ``999``, ``field[2]`` = ``'C'``,
        ``field[3]`` = ``3``.

        In the update:

        .. code-block:: lua

            box.space.tester:update({999}, {{'#', 2, 1}, {'-', 2, 3}})

        The idea is to delete ``field[2]``, then subtract ``3`` from ``field[3]``.
        But after the delete, there is a renumbering, so ``field[3]`` becomes
        ``field[2]``` before we subtract ``3`` from it, and that's why the
        seventh argument is ``2``, not ``3``. Therefore, after this update,
        ``field[1]`` = ``999``, ``field[2]`` = ``0``.

        In the update:

        .. code-block:: lua

            box.space.tester:update({999}, {{'=', 2, 'XYZ'}})

        we're making a long string so that splice will work in the next example.
        Therefore, after this update, ``field[1]`` = ``999``, ``field[2]`` = ``'XYZ'``.

        In the update

        .. code-block:: lua

            box.space.tester:update({999}, {{':', 2, 2, 1, '!!'}})

        The third argument is ``':'``, that is, this is the example of splice.
        The fourth argument is ``2`` because the change will occur in ``field[2]``.
        The fifth argument is 2 because deletion will begin with the second byte.
        The sixth argument is 1 because the number of bytes to delete is 1.
        The seventh argument is ``'!!'``, because ``'!!'`` is to be added at this position.
        Therefore, after this update, ``field[1]`` = ``999``, ``field[2]`` = ``'X!!Z'``.

    .. _space_upsert:

    .. method:: upsert(tuple_value, {{operator, field_no, value}, ...}, )

        Update or insert a tuple.

        If there is an existing tuple which matches the key fields of ``tuple_value``, then the
        request has the same effect as :func:`space_object:update() <space_object.update>` and the
        ``{{operator, field_no, value}, ...}`` parameter is used.
        If there is no existing tuple which matches the key fields of ``tuple_value``, then the
        request has the same effect as :func:`space_object:insert() <space_object.insert>` and the
        ``{tuple_value}`` parameter is used. However, unlike ``insert`` or
        ``update``, ``upsert`` will not read a tuple and perform
        error checks before returning -- this is a design feature which
        enhances throughput but requires more caution on the part of the user.

        Parameters: :samp:`{space_object}` = an :ref:`object reference <object-reference>`;
        :samp:`{tuple_value}` (type = Lua table or scalar) =
        field values, must be passed as a Lua table;
        :codeitalic:`{operator, field_no, value}` (type = Lua table) = a group of arguments for each
        operation, indicating what the operation is, what field the
        operation will apply to, and what value will be applied. The
        field number can be negative, meaning the position from the end of
        the tuple (#tuple + negative field number + 1).

        :return: null.

        Possible errors: it is illegal to modify a primary-key field.
        It is illegal to use upsert with a space that has a unique secondary index.

        **Complexity factors:** Index size, Index type, number of indexes accessed, WAL
        settings.

        **Example:**

            .. code-block:: lua

                box.space.tester:upsert({12,'c'}, {{'=', 3, 'a'}, {'=', 4, 'b'}})

    .. _space_delete:

    .. method:: delete(key)

        Delete a tuple identified by a primary key.

        Parameters: :samp:`{space_object}` = an :ref:`object reference <object-reference>`
        :codeitalic:`key` (type = Lua table or scalar) = key to be matched against the index
        key, which may be multi-part.

        :return: the deleted tuple
        :rtype:  tuple

        **Complexity Factors:** Index size, Index type

        Note re storage engine: phia will return nil, rather than the deleted tuple.

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> box.space.tester:delete(1)
            ---
            - [1, 'My first tuple']
            ...
            tarantool> box.space.tester:delete(1)
            ---
            ...
            tarantool> box.space.tester:delete('a')
            ---
            - error: 'Supplied key type of part 0 does not match index part type:
              expected NUM'
            ...

    .. data:: id

        Ordinal space number. Spaces can be referenced by either name or
        number. Thus, if space ``tester`` has ``id = 800``, then
        ``box.space.tester:insert{0}`` and ``box.space[800]:insert{0}``
        are equivalent requests.

        Parameters: :samp:`{space_object}` = an :ref:`object reference <object-reference>`.

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> box.space.tester.id
            ---
            - 512
            ...

    .. data:: enabled

        Whether or not this space is enabled.
        The value is ``false`` if the space has no index.

        Parameters: :samp:`{space_object}` = an :ref:`object reference <object-reference>`.

    .. _space-object-field-count:

    .. data:: field_count

        The required field count for all tuples in this space. The field_count
        can be set initially with:

        .. cssclass:: highlight
        .. parsed-literal::

            box.schema.space.create(..., {
                ... ,
                field_count = *field_count_value* ,
                ...
            })

        The default value is ``0``, which means there is no required field count.

        Parameters: :samp:`{space_object}` = an :ref:`object reference <object-reference>`.

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> box.space.tester.field_count
            ---
            - 0
            ...

    .. data:: index

        A container for all defined indexes. There is a Lua object of type
        :ref:`box.index <box_index>` with methods to search tuples and iterate over them in
        predefined order.

        Parameters: :samp:`{space_object}` = an :ref:`object reference <object-reference>`.

        :rtype: table

        **Example:**

        .. code-block:: lua

            tarantool> #box.space.tester.index
            ---
            - 1
            ...
            tarantool> box.space.tester.index.primary.type
            ---
            - TREE
            ...

    .. method:: count([key], [iterator])

        Parameters: :samp:`{space_object}` = an :ref:`object reference <object-reference>`;
        :codeitalic:`key` (type = Lua table or scalar) = key to be matched against the primary index
        key, which may be multi-part; :codeitalic:`iterator` = comparison method.

        :return: Number of tuples.

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> box.space.tester:count(2, {iterator='GE'})
            ---
            - 1
            ...

    Note re storage engine: phia does not support :codenormal:`count(...)`.
    One possible workaround is to say :codenormal:`#select(...)`.


    .. method:: len()

        Parameters: :samp:`{space_object}` = an :ref:`object reference <object-reference>`.

        :return: Number of tuples in the space.

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> box.space.tester:len()
            ---
            - 2
            ...

    .. _space_truncate:

    .. method:: truncate()

        Deletes all tuples.

        Parameters: :samp:`{space_object}` = an :ref:`object reference <object-reference>`.

        **Complexity Factors:** Index size, Index type, Number of tuples accessed.

        :return: nil

        Note re storage engine: phia does not support ``truncate``.

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> box.space.tester:truncate()
            ---
            ...
            tarantool> box.space.tester:len()
            ---
            - 0
            ...

    .. method:: inc{field-value [, field-value ...]}

        Increments a counter in a tuple whose primary key matches the
        field-value(s). The field following the primary-key fields
        will be the counter. If there is no tuple matching the
        ``field-value(s)``, a new one is inserted with initial counter
        value set to ``1``.


        Parameters: :samp:`{space_object}` = an :ref:`object reference <object-reference>`;
        :codeitalic:`field-value(s)` (type = Lua table or scalar) = values which must match the primary key.

        :return: the new counter value
        :rtype:  number

        **Complexity Factors:** Index size, Index type, WAL settings.

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> s = box.schema.space.create('forty_second_space')
            ---
            ...
            tarantool> s:create_index('primary', {
                     >   unique = true,
                     >   parts = {1, 'NUM', 2, 'STR'}
                     > })
            ---
            ...
            tarantool> box.space.forty_second_space:inc{1, 'a'}
            ---
            - 1
            ...
            tarantool> box.space.forty_second_space:inc{1, 'a'}
            ---
            - 2
            ...

    .. method:: dec{field-value [, field-value ...]}

        Decrements a counter in a tuple whose primary key matches the
        ``field-value(s)``. The field following the primary-key fields
        will be the counter. If there is no tuple matching the
        ``field-value(s)``, a new one is not inserted. If the counter value drops
        to zero, the tuple is deleted.

        Parameters: :samp:`{space_object}` = an :ref:`object reference <object-reference>`;
        :codeitalic:`field-value(s)` (type = Lua table or scalar) = values which must match the primary key.

        :return: the new counter value
        :rtype:  number

        **Complexity factors:** Index size, Index type, WAL settings.

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> s = box.schema.space.create('space19')
            ---
            ...
            tarantool> s:create_index('primary', {
                     >   unique = true,
                     >   parts = {1, 'NUM', 2, 'STR'}
                     > })
            ---
            ...
            tarantool> box.space.space19:insert{1, 'a', 1000}
            ---
            - [1, 'a', 1000]
            ...
            tarantool> box.space.space19:dec{1, 'a'}
            ---
            - 999
            ...
            tarantool> box.space.space19:dec{1, 'a'}
            ---
            - 998
            ...

    .. _space_auto_increment:

    .. method:: auto_increment{field-value [, field-value ...]}

        Insert a new tuple using an auto-increment primary key. The space specified
        by space_object must have a ``NUM`` primary key index of type ``TREE``. The
        primary-key field will be incremented before the insert.
        Note re storage engine: phia does not support auto_increment.

        Parameters: :samp:`{space_object}` = an :ref:`object reference <object-reference>`;
        :codeitalic:`field-value(s)` (type = Lua table or scalar) = tuple's fields, other than the primary-key field.

        :return: the inserted tuple.
        :rtype:  tuple

        **Complexity Factors:** Index size, Index type,
        Number of indexes accessed, WAL settings.

        Possible errors: index has wrong type or primary-key indexed field is not a number.

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> box.space.tester:auto_increment{'Fld#1', 'Fld#2'}
            ---
            - [1, 'Fld#1', 'Fld#2']
            ...
            tarantool> box.space.tester:auto_increment{'Fld#3'}
            ---
            - [2, 'Fld#3']
            ...

    .. method:: pairs()

        A helper function to prepare for iterating over all tuples in a space.

        Parameters: :samp:`{space_object}` = an :ref:`object reference <object-reference>`.

        :return: function which can be used in a for/end loop. Within the loop, a value is returned for each iteration.
        :rtype:  function, tuple

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> s = box.schema.space.create('space33')
            ---
            ...
            tarantool> -- index 'X' has default parts {1, 'NUM'}
            tarantool> s:create_index('X', {})
            ---
            ...
            tarantool> s:insert{0, 'Hello my '}, s:insert{1, 'Lua world'}
            ---
            - [0, 'Hello my ']
            - [1, 'Lua world']
            ...
            tarantool> tmp = ''
            ---
            ...
            tarantool> for k, v in s:pairs() do
                     >   tmp = tmp .. v[2]
                     > end
            ---
            ...
            tarantool> tmp
            ---
            - Hello my Lua world
            ...

.. data:: _schema

    ``_schema`` is a system tuple set. Its single tuple contains these fields:
    ``'version', major-version-number, minor-version-number``.

    **Example:**

    The following function will display all fields in all tuples of ``_schema``:

    .. code-block:: lua

        function example()
          local ta = {}
          local i, line
          for k, v in box.space._schema:pairs() do
            i = 1
            line = ''
            while i <= #v do
              line = line .. v[i] .. ' '
              i = i + 1
            end
            table.insert(ta, line)
          end
          return ta
        end

    Here is what ``example()`` returns in a typical installation:

    .. code-block:: tarantoolsession

        tarantool> example()
        ---
        - - 'cluster 1ec4e1f8-8f1b-4304-bb22-6c47ce0cf9c6 '
          - 'max_id 520 '
          - 'version 1 6 '
        ...

.. data:: _space

    ``_space`` is a system tuple set. Its tuples contain these fields: ``id``,
    ``uid``, ``space-name``, ``engine``, ``field_count``, ``temporary``, ``format``.
    These fields are established by :func:`space.create() <box.schema.space.create>`.

    **Example:**

    The following function will display all simple fields in all tuples of ``_space``.

    .. code-block:: lua_tarantool

        function example()
          local ta = {}
          local i, line
          for k, v in box.space._space:pairs() do
            i = 1
            line = ''
            while i <= #v do
              if type(v[i]) ~= 'table' then
                line = line .. v[i] .. ' '
              end
            i = i + 1
            end
            table.insert(ta, line)
          end
          return ta
        end

    Here is what ``example()`` returns in a typical installation:

    .. code-block:: tarantoolsession

        tarantool> example()
        ---
        - - '272 1 _schema memtx 0  '
          - '280 1 _space memtx 0  '
          - '281 1 _vspace sysview 0  '
          - '288 1 _index memtx 0  '
          - '296 1 _func memtx 0  '
          - '304 1 _user memtx 0  '
          - '305 1 _vuser sysview 0  '
          - '312 1 _priv memtx 0  '
          - '313 1 _vpriv sysview 0  '
          - '320 1 _cluster memtx 0  '
          - '512 1 tester memtx 0  '
          - '513 1 origin phia 0  '
          - '514 1 archive memtx 0  '
        ...

    **Example:**

    The following requests will create a space using
    ``box.schema.space.create`` with a ``format`` clause.
    Then it retrieves the _space tuple for the new space.
    This illustrates the typical use of the ``format`` clause,
    it shows the recommended names and data types for the fields.

    .. code-block:: tarantoolsession

        tarantool> box.schema.space.create('TM', {
                 >   format = {
                 >     [1] = {["name"] = "field#1"},
                 >     [2] = {["type"] = "num"}
                 >   }
                 > })
        ---
        - index: []
          on_replace: 'function: 0x41c67338'
          temporary: false
          id: 522
          engine: memtx
          enabled: false
          name: TM
          field_count: 0
        - created
        ...
        tarantool> box.space._space:select(522)
        ---
        - - [522, 1, 'TM', 'memtx', 0, '', [{'name': 'field#1'}, {'type': 'num'}]]
        ...

.. data:: _index

    ``_index`` is a system tuple set. Its tuples contain these fields:
    ``space-id index-id index-name index-type index-is-unique
    index-field-count [tuple-field-no, tuple-field-type ...]``.

    The following function will display some fields in all tuples of ``_index``:

    .. code-block:: lua

        function example()
          local ta = {}
          local i, line
          for k, v in box.space._index:pairs() do
            i = 1
            line = ''
            while i <= 4 do
                line = line .. v[i] .. ' '
                i = i + 1
            end
            table.insert(ta, line)
            end
          return ta
        end

    Here is what ``example()`` returns in a typical installation:

    .. code-block:: tarantoolsession

        tarantool> example()
        ---
        - - '272 0 primary tree 1 1 0 str '
          - '280 0 primary tree 1 1 0 num '
          - '280 1 owner tree 0 1 1 num '
          - '280 2 name tree 1 1 2 str '
          - '288 0 primary tree 1 2 0 num 1 num '
          - '288 2 name tree 1 2 0 num 2 str '
          - '296 0 primary tree 1 1 0 num '
          - '296 1 owner tree 0 1 1 num '
          - '296 2 name tree 1 1 2 str '
          - '304 0 primary tree 1 1 0 num '
          - '304 1 owner tree 0 1 1 num '
          - '304 2 name tree 1 1 2 str '
          - '312 0 primary tree 1 3 1 num 2 str 3 num '
          - '312 1 owner tree 0 1 0 num '
          - '312 2 object tree 0 2 2 str 3 num '
          - '313 0 primary tree '
          - '313 1 owner tree '
          - '313 2 object tree '
          - '320 0 primary tree 1 1 0 num '
          - '320 1 uuid tree 1 1 1 str '
          - '512 0 primary tree 1 1 0 num '
          - '513 0 first tree 1 1 0 NUM '
          - '514 0 first tree 1 1 0 STR '
        ...

.. data:: _user

    ``_user`` is a system tuple set for
    support of the :ref:`authorization feature <box-authentication>`.

.. data:: _priv

    ``_priv`` is a system tuple set for
    support of the :ref:`authorization feature <box-authentication>`.

.. data:: _cluster

    ``_cluster`` is a system tuple set
    for support of the :ref:`replication feature <box-replication>`.

=============================================================================
          Example: use box.space functions to read _space tuples
=============================================================================

This function will illustrate how to look at all the spaces, and for each
display: approximately how many tuples it contains, and the first field of
its first tuple. The function uses Tarantool ``box.space`` functions ``len()``
and ``pairs()``. The iteration through the spaces is coded as a scan of the
``_space`` system tuple set, which contains metadata. The third field in
``_space`` contains the space name, so the key instruction
``space_name = v[3]`` means ``space_name`` is the ``space_name`` field in
the tuple of ``_space`` that we've just fetched with ``pairs()``. The function
returns a table:

.. code-block:: lua

    function example()
      local tuple_count, space_name, line
      local ta = {}
      for k, v in box.space._space:pairs() do
        space_name = v[3]
        if box.space[space_name].index[0] ~= nil then
          tuple_count = '1 or more'
        else
          tuple_count = '0'
        end
        line = space_name .. ' tuple_count =' .. tuple_count
        if tuple_count == '1 or more' then
          for k1, v1 in box.space[space_name]:pairs() do
            line = line .. '. first field in first tuple = ' .. v1[1]
            break
          end
        end
        table.insert(ta, line)
      end
      return ta
    end

And here is what happens when one invokes the function:

.. code-block:: tarantoolsession

    tarantool> example()
    ---
    - - _schema tuple_count =1 or more. first field in first tuple = cluster
      - _space tuple_count =1 or more. first field in first tuple = 272
      - _vspace tuple_count =1 or more. first field in first tuple = 272
      - _index tuple_count =1 or more. first field in first tuple = 272
      - _vindex tuple_count =1 or more. first field in first tuple = 272
      - _func tuple_count =1 or more. first field in first tuple = 1
      - _vfunc tuple_count =1 or more. first field in first tuple = 1
      - _user tuple_count =1 or more. first field in first tuple = 0
      - _vuser tuple_count =1 or more. first field in first tuple = 0
      - _priv tuple_count =1 or more. first field in first tuple = 1
      - _vpriv tuple_count =1 or more. first field in first tuple = 1
      - _cluster tuple_count =1 or more. first field in first tuple = 1
    ...

===========================================================================
          Example: use box.space functions to organize a _space tuple
===========================================================================

The objective is to display field names and field types of a system space --
using metadata to find metadata.

To begin: how can one select the _space tuple that describes _space?

A simple way is to look at the constants in box.schema,
which tell us that there is an item named SPACE_ID == 288,
so these statements will retrieve the correct tuple: |br|
:codenormal:`box.space._space:select{288}` |br|
or |br|
:codenormal:`box.space._space:select{box.schema.SPACE_ID}` |br|

Another way is to look at the tuples in box.space._index,
which tell us that there is a secondary index named 'name' for space
number 288, so this statement also will retrieve the correct tuple: |br|
:codenormal:`box.space._space.index.name:select{'_space'}`

However, the retrieved tuple is not easy to read: |br|
:codenormal:`tarantool>` :codebold:`box.space._space.index.name:select{'_space'}` |br|
:codenormal:`---` |br|
:codenormal:`- - [280, 1, '_space', 'memtx', 0, '', [{'name': 'id',` |br|
|nbsp| |nbsp| |nbsp| :codenormal:`'type': 'num'}, {'name': 'owner','type': 'num'},` |br|
|nbsp| |nbsp| |nbsp| :codenormal:`{'name': 'name','type': 'str'}, {'name': 'engine',` |br|
|nbsp| |nbsp| |nbsp| :codenormal:`'type': 'str'},{'name': 'field_count', 'type': 'num'},` |br|
|nbsp| |nbsp| |nbsp| :codenormal:`{'name': 'flags','type': 'str'}, {'name': 'format',` |br|
|nbsp| |nbsp| |nbsp| :codenormal:`'type': '*'}]]` |br|
|nbsp| |nbsp| |nbsp| :codenormal:`...`

It looks disorganized because field number 7
has been formatted with recommended names and data types.
How can one get those specific sub-fields?
Since it's visible that field number 7 is an array of maps,
this `for` loop will do the organizing: |br|
:codenormal:`local tuple_of_space, field_name, field_type` |br|
:codenormal:`tuple_of_space = box.space._space.index.name:select{'_space'}[1]` |br|
:codenormal:`for i = 1, #tuple_of_space[7], 1` |br|
:codenormal:`do` |br|
|nbsp| |nbsp| |nbsp| :codenormal:`field_name = tuple_of_space[7][i]['name']` |br|
|nbsp| |nbsp| |nbsp| :codenormal:`field_type = tuple_of_space[7][i]['type']` |br|
|nbsp| |nbsp| |nbsp| :codenormal:`print(field_name .. ',' ..field_type)` |br|
:codenormal:`end`

And here is what happens when one executes the `for` loop: |br|
:codenormal:`id,num` |br|
:codenormal:`owner,num` |br|
:codenormal:`name,str` |br|
:codenormal:`engine,str` |br|
:codenormal:`field_count,num` |br|
:codenormal:`flags,str` |br|
:codenormal:`format,*`



